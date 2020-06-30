// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_TYPES_H_
#define ADMMPD_TYPES_H_

#include <Eigen/Geometry>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <vector>

// TODO template type for float/double

namespace admmpd {
template <typename T> using RowSparseMatrix = Eigen::SparseMatrix<T,Eigen::RowMajor>;

struct Options {
    double timestep_s; // TODO: Figure out delta time from blender api
    int max_admm_iters;
    int max_cg_iters;
    int max_gs_iters;
    double mult_k; // stiffness multiplier for constraints
    double min_res; // min residual for CG solver
    double youngs; // Young's modulus // TODO variable per-tet
    double poisson; // Poisson ratio // TODO variable per-tet
    Eigen::Vector3d grav;
    Options() :
        timestep_s(1.0/24.0),
        max_admm_iters(50),
        max_cg_iters(10),
        max_gs_iters(30),
        mult_k(1),
        min_res(1e-6),
        youngs(1000000),
        poisson(0.299),
        grav(0,0,-9.8)
        {}
};

// I think eventually I'll make the mesh
// a virtual class with mapping functions.
// That might clean up the API/interface a bit.
// Will work out what we need for collisions and such first.

struct TetMeshData {
    Eigen::MatrixXd x_rest; // verts at rest
    Eigen::MatrixXi faces; // surface elements, m x 3
    Eigen::MatrixXi tets; // internal elements, m x 4
};

struct EmbeddedMeshData { // i.e. the lattice
    Eigen::MatrixXd x_rest; // embedded verts at rest
    Eigen::MatrixXi faces; // embedded faces
    Eigen::MatrixXi tets; // lattice elements, m x 4
    Eigen::VectorXi vtx_to_tet; // what tet vtx is embedded in, p x 1
    Eigen::MatrixXd barys; // barycoords of the embedding, p x 4
}; // type 1

struct SolverData {
    // Set from input
    Eigen::MatrixXi tets; // elements t x 4, copy from mesh
    Eigen::MatrixXd x; // vertices, n x 3
    Eigen::MatrixXd v; // velocity, n x 3
    // Set in compute_matrices: 
    Eigen::MatrixXd x_start; // x at beginning of timestep, n x 3
    Eigen::VectorXd m; // masses, n x 1
    Eigen::MatrixXd z; // ADMM z variable
    Eigen::MatrixXd u; // ADMM u aug lag with W inv
    Eigen::MatrixXd M_xbar; // M*(x + dt v)
    Eigen::MatrixXd Dx; // D * x
    Eigen::MatrixXd b; // M xbar + DtW2(z-u)
    RowSparseMatrix<double> D; // reduction matrix
    RowSparseMatrix<double> DtW2; // D'W^2
    RowSparseMatrix<double> A; // M + D'W^2D
    RowSparseMatrix<double> K[3]; // constraint Jacobian
    Eigen::VectorXd l; // constraint rhs (Kx=l)
    double spring_k; // constraint stiffness
	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double> > ldltA;
    struct CGData { // Temporaries used in conjugate gradients
        RowSparseMatrix<double> A[3]; // (M + D'W^2D) + k * Kt K
        Eigen::MatrixXd b; // M xbar + DtW2(z-u) + k Kt l
        Eigen::MatrixXd r; // residual
        Eigen::MatrixXd z;
        Eigen::MatrixXd p;
        Eigen::MatrixXd Ap; // A * p
    } cgdata;
    struct GSData { // Temporaries used in Gauss-Seidel
		RowSparseMatrix<double> KtK; // k * Kt K, different dim than A!
        Eigen::MatrixXd last_dx; // last GS iter change in x
		std::vector<std::vector<int> > A_colors; // colors of just A matrix
        std::vector<std::vector<int> > A_KtK_colors; // colors of just A+KtK
	} gsdata;
    // Set in append_energies:
	std::vector<Eigen::Vector2i> indices; // per-energy index into D (row, num rows)
	std::vector<double> rest_volumes; // per-energy rest volume
	std::vector<double> weights; // per-energy weights
};

} // namespace admmpd

#endif // ADMMPD_TYPES_H_
