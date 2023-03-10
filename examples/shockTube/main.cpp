#include <fvhyper/mesh.h>
#include <fvhyper/explicit.h>
#include <fvhyper/parallel.h>
#include <fvhyper/post.h>


/*
    Implementation of the Sod shock tube problem using fvhyper
*/
namespace fvhyper {


    // Define global constants
    const int vars = 4;
    const std::vector<std::string> var_names = {
        "rho",
        "rhou",
        "rhov",
        "rhoe"
    };
    namespace solver {
        const bool do_calc_gradients = false;
        const bool do_calc_limiters = false;
        const bool linear_interpolate = false;
        const bool diffusive_gradients = false;
        const bool global_dt = true;
        const bool smooth_residuals = false;
    }

    namespace consts {
        double gamma = 1.4;
    }

    /*
        Define initial solution
        Here, we have a non-uniform initial condition corresponding to
        the sod shock tube problem
        https://en.wikipedia.org/wiki/Sod_shock_tube
    */
    void generate_initial_solution(
        std::vector<double>& v,
        const mesh& m
    ) {
        for (uint i=0; i<m.cellsAreas.size(); ++i) {
            if (m.cellsCentersX[i] < 0.5) {
                v[4*i] = 1.;
                v[4*i+1] = 0.;
                v[4*i+2] = 0.;
                v[4*i+3] = 1./(consts::gamma-1);
            } else {
                v[4*i] = 0.125;
                v[4*i+1] = 0.;
                v[4*i+2] = 0.;
                v[4*i+3] = 0.1/(consts::gamma-1);
            }
        }
    }

    // First order, no limiters
    double limiter_func(const double& r) {
        return 0.;
    }

    /*
        Define flux function for the euler equations
        Roe flux vector differencing
    */
    void calc_flux(
        double* f,
        const double* qi,
        const double* qj,
        const double* gx,
        const double* gy,
        const double* n
    ) {

        // Central flux
        double pi, pj, Vi, Vj;
        pi = (consts::gamma - 1)*(qi[3] - 0.5/qi[0]*(qi[1]*qi[1] + qi[2]*qi[2]));
        pj = (consts::gamma - 1)*(qj[3] - 0.5/qj[0]*(qj[1]*qj[1] + qj[2]*qj[2]));
        Vi = (qi[1]*n[0] + qi[2]*n[1])/qi[0];
        Vj = (qj[1]*n[0] + qj[2]*n[1])/qj[0];

        f[0] = qi[0]*Vi;
        f[1] = qi[1]*Vi + pi*n[0];
        f[2] = qi[2]*Vi + pi*n[1];
        f[3] = (qi[3] + pi)*Vi;

        f[0] += qj[0]*Vj;
        f[1] += qj[1]*Vj + pj*n[0];
        f[2] += qj[2]*Vj + pj*n[1];
        f[3] += (qj[3] + pj)*Vj;

        for (uint i=0; i<4; ++i) f[i] *= 0.5;

        // Upwind flux
        const double pL = pi;
        const double pR = pj;

        // Roe variables
        const double uL = qi[1]/qi[0];
        const double uR = qj[1]/qj[0];
        const double vL = qi[2]/qi[0];
        const double vR = qj[2]/qj[0];

        const double srhoL = sqrt(qi[0]);
        const double srhoR = sqrt(qj[0]);
        const double rho = srhoR*srhoL;
        const double u = (uL*srhoL + uR*srhoR)/(srhoL + srhoR);
        const double v = (vL*srhoL + vR*srhoR)/(srhoL + srhoR);
        const double h = ((qi[3] + pL)/qi[0]*srhoL + (qj[3] + pR)/qj[0]*srhoR)/(srhoL + srhoR);
        const double q2 = u*u + v*v;
        const double c = sqrt( (consts::gamma - 1.) * (h - 0.5*q2) );
        const double V = u*n[0] + v*n[1];
        const double VR = uR*n[0] + vR*n[1];
        const double VL = uL*n[0] + vL*n[1];

        // Roe correction
        // From https://www.researchgate.net/publication/305638346_Cures_for_the_Expansion_Shock_and_the_Shock_Instability_of_the_Roe_Scheme
        const double lambda_cm = abs(std::min(V-c, VL-c));
        const double lambda_c  = abs(V);
        const double lambda_cp = abs(std::max(V+c, VR+c));

        const double kF1 = lambda_cm*((pR-pL) - rho*c*(VR-VL))/(2.*c*c);
        const double kF234_0 = lambda_c*((qj[0] - qi[0]) - (pR-pL)/(c*c));
        const double kF234_1 = lambda_c*rho;
        const double kF5 = lambda_cp*((pR-pL) + rho*c*(VR-VL))/(2*c*c);

        // Roe flux

        f[0] -= 0.5*(kF1            + kF234_0                                                       + kF5);
        f[1] -= 0.5*(kF1*(u-c*n[0]) + kF234_0*u      + kF234_1*(uR - uL - (VR-VL)*n[0])             + kF5*(u+c*n[0]));
        f[2] -= 0.5*(kF1*(v-c*n[1]) + kF234_0*v      + kF234_1*(vR - vL - (VR-VL)*n[1])             + kF5*(v+c*n[1]));
        f[3] -= 0.5*(kF1*(h-c*V)    + kF234_0*q2*0.5 + kF234_1*(u*(uR-uL) + v*(vR-vL) - V*(VR-VL))  + kF5*(h+c*V)); 
    }

    /*
        Define the time step. Here, time step is constant
    */
    void calc_dt(
        std::vector<double>& dt,
        const std::vector<double>& q,
        mesh& m
    ) {
        // Constant time step
        for (auto& dti : dt) {dti = 2e-5;}
    }

    /*
        Define the boundary conditions
    */
    namespace boundaries {

        /*
            Define boundary conditions
            We apply a neumann zero flux condition to all boundaries
        */
        void zero_flux(double* b, double* q, double* n) {
            b[0] = q[0];
            b[1] = q[1];
            b[2] = q[2];
            b[3] = q[3];
        }
        std::map<std::string, void (*)(double*, double*, double*)> 
            bounds = {
                {"wall", zero_flux}
            };
    }


    /*
        Define extra output variables
    */
    namespace post {
        void calc_output_u(double* u, double* q) {
            // Compute vector u
            u[0] = q[1] / q[0];
            u[1] = q[2] / q[0];
        }

        void calc_output_p(double* p, double* q) {
            // Compute pressure p
            p[0] = (consts::gamma - 1.)*(q[3] - 0.5/q[0]*(q[1]*q[1] + q[2]*q[2]));
        }

        std::map<std::string, void (*)(double*, double*)> 
        extra_scalars = {
            {"p", calc_output_p}
        };
        
        std::map<std::string, void (*)(double*, double*)> 
        extra_vectors = {
            {"U", calc_output_u}
        };
    }


}



int main() {
    // Initialize the MPI environment
    fvhyper::mpi_wrapper pool;

    // Create mesh object m
    fvhyper::mesh m;

    /*
        Define problem files name
        mesh files must be name according to:
            name_{rank+1}.msh
        where rank is the mpi rank associated with this mesh process
        per example, for 3 mpi ranks, we would have the files:
            name_1.msh  name_2.msh  name_3.msh
    */
    std::string name = "square";

    // Read the file
    m.read_file(name, pool);

    fvhyper::solverOptions options;
    options.max_step = 10000;
    options.max_time = 0.2;
    options.print_interval = 100;

    // Run solver
    std::vector<double> q;
    fvhyper::run(name, q, pool, m, options);

    // Save file
    fvhyper::writeVtk(name, q, m, pool.rank, pool.size);

    return pool.exit();
}

