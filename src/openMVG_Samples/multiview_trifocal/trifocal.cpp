// Copyright (c) 2019 Pierre MOULON.
//:\file
//\author Pierre MOULON
//\author Gabriel ANDRADE Rio de Janeiro State U.
//\author Ricardo Fabbri, Brown & Rio de Janeiro State U. (rfabbri.github.io) 

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <numeric>

//these are temporary includes, may be removed
#include <Eigen/StdVector>
#include "openMVG/multiview/projection.hpp"
#include "openMVG/multiview/triangulation.hpp"

#include "minus/minus.hxx"
#include "minus/chicago-default.h"
#include "minus/chicago14a-default-data.hxx"

#include "trifocal-util.h"
#include "trifocal.h"


// Mat is Eigen::MatrixXd - matrix of doubles with dynamic size
// Vec3 is Eigen::Vector3d - Matrix< double, 3, 1 >

namespace trifocal3pt {
  
int iteration_global_debug = 0;
  
using namespace std;
using namespace MiNuS;
using namespace openMVG;

//-------------------------------------------------------------------------------
//
// Global variables
// 
// constexpr unsigned n_ids = 5;
// unsigned desired_ids[n_ids] = {13, 23, 33, 93, 53};
//-------------------------------------------------------------------------------

//-------------------------------------------------------------------------------
// Trifocal3PointPositionTangentialSolver
//-------------------------------------------------------------------------------

void Trifocal3PointPositionTangentialSolver::
Solve(
      const Mat &datum_0,
      const Mat &datum_1,
      const Mat &datum_2,
      std::vector<trifocal_model_t> *trifocal_tensor)
{
  double p[io::pp::nviews][io::pp::npoints][io::ncoords2d];
  double tgt[io::pp::nviews][io::pp::npoints][io::ncoords2d]; 
  
  //std::cerr << "Datum 0 mtrx: " << datum_0.rows() << "X" << datum_0.cols() << endl;
  std::cerr << "TRIFOCAL LOG: Called Solve()\n";
  // pack into solver's efficient representation
  for (unsigned ip=0; ip < io::pp::npoints; ++ip) {
      p[0][ip][0] = datum_0(0,ip);
      p[0][ip][1] = datum_0(1,ip);
    tgt[0][ip][0] = datum_0(2,ip); 
    tgt[0][ip][1] = datum_0(3,ip); 
    
      p[1][ip][0] = datum_1(0,ip);
      p[1][ip][1] = datum_1(1,ip);
    tgt[1][ip][0] = datum_1(2,ip); 
    tgt[1][ip][1] = datum_1(3,ip); 
    
      p[2][ip][0] = datum_2(0,ip);
      p[2][ip][1] = datum_2(1,ip);
    tgt[2][ip][0] = datum_2(2,ip); 
    tgt[2][ip][1] = datum_2(3,ip); 
  }
  
  unsigned nsols_final = 0;
  unsigned id_sols[M::nsols];
  double  cameras[M::nsols][io::pp::nviews-1][4][3];  // first camera is always [I | 0]
  
  std::cerr << "TRIFOCAL LOG: Before minus::solve()\n" << std::endl;
  MiNuS::minus<chicago>::solve(p, tgt, cameras, id_sols, &nsols_final);
  if(cameras){
    // fill C0* with for loop
    std::cerr << "Number of sols " << nsols_final << std::endl;
    std::vector<trifocal_model_t> &tt = *trifocal_tensor; // if I use the STL container, 
    // This I would have to change the some other pieces of code, maybe altering the entire logic of this program!!
    // std::cerr << "TRIFOCAL LOG: Antes de resize()\n" << std::endl;
    tt.resize(nsols_final);
    std::cerr << "TRIFOCAL LOG: Chamou resize()\n";
    //using trifocal_model_t = array<Mat34, 3>;
    for (unsigned s=0; s < nsols_final; ++s) {
      tt[s][0] = Mat34::Identity(); // view 0 [I | 0]
      for (unsigned v=1; v < io::pp::nviews; ++v) {
          memcpy(tt[s][v].data(), (double *) cameras[id_sols[s]][v], 9*sizeof(double));
          for (unsigned r=0; r < 3; ++r)
            tt[s][v](r,3) = cameras[id_sols[s]][v][3][r];
      }
    }
    // TODO: filter the solutions by:
    // - positive depth and 
    // - using tangent at 3rd point
    //
    // If we know the rays are perfectly coplanar, we can just use cross
    // product within the plane instead of SVD
    std::cerr << "TRIFOCAL LOG: Finished ()Solve()\n";
  } else{
    std::cerr << "Minus failed to compute tracks\n";
    exit(EXIT_FAILURE);
  } 
}

double Trifocal3PointPositionTangentialSolver::
Error(
  const trifocal_model_t &tt,
  const Vec &bearing_0, // x,y,tangentialx,tangentialy
  const Vec &bearing_1,
  const Vec &bearing_2,
  const Vec &pxbearing_0,
  const Vec &pxbearing_1,
  const Vec &pxbearing_2,
  const double K[2][3]) 
{
  // Return the cost related to this model and those sample data point
  // Ideal algorithm:
  // 1) reconstruct the 3D points and orientations
  // 2) project the 3D points and orientations on all images_
  // 3) compute error 
  // 
  // In practice we ignore the directions and only reproject to one third view
  std::cerr << "TRIFOCAL LOG: Entered error()\n";
  // 3x3: each column is x,y,1
  Mat3 bearing;
  // std::cerr << "bearing_0 head homogeneous \n" << bearing_0.head(2).homogeneous() << std::endl;
  bearing << bearing_0.head(2).homogeneous(),
             bearing_1.head(2).homogeneous(), 
             bearing_2.head(2).homogeneous();
  // std::cerr << "bearing mtrx:\n " << bearing << endl;
  // Using triangulation.hpp
  Vec4 triangulated_homg;
  unsigned third_view = 0;
  std::cerr << "tt[0] mtrx:\n " << tt[0] << endl;
  std::cerr << "tt[1] mtrx:\n " << tt[1] << endl;
  std::cerr << "tt[2] mtrx:\n " << tt[2] << endl;
  std::cerr << "0 mtrx:\n " << Eigen::MatrixXd::Zero(3,3) << endl;
  if(tt[0] == Eigen::MatrixXd::Zero(3,4) || tt[1] == Eigen::MatrixXd::Zero(3,4) || tt[2] == Eigen::MatrixXd::Zero(3,4)){
  
     std::cerr << "0 Matrix Found\n";
     exit(EXIT_FAILURE); 
  }else{
    // pick the wider baseline. TODO: measure all pairwise translation distances
    if (tt[1].col(3).squaredNorm() > tt[2].col(3).squaredNorm()) {
      // TODO use triangulation from the three views at once
      TriangulateDLT(tt[0], bearing.col(0), tt[1], bearing.col(1), &triangulated_homg);
      third_view = 2;
      std::cerr << "triangulated_homg mtrx:\n " << triangulated_homg << endl;
    } else {
      TriangulateDLT(tt[0], bearing.col(0), tt[2], bearing.col(2), &triangulated_homg);
      std::cerr << "triangulated_homg mtrx:\n " << triangulated_homg << endl;
      third_view = 1;
    }
    std::cerr << "tt[0] mtrx:\n " << tt[0] << endl;
    std::cerr << "tt[1] mtrx:\n " << tt[1] << endl;
    std::cerr << "tt[2] mtrx:\n " << tt[2] << endl;
    Mat2 pxbearing;
    // std::cerr << "pxbearing 0 mtrx: " << pxbearing_0.head(2).rows() << "X" << pxbearing_0.head(2).cols() << endl;
    pxbearing << pxbearing_0.head(1).homogeneous(),
                 pxbearing_1.head(1).homogeneous();
    // std::cerr << "pxbearing mtrx:\n " << pxbearing << endl;
    // Computing the projection of triangulated points using projection.hpp
    // For prototyping and speed, for now we will only project to the third view
    // and report only one error
    Vec2 pxreprojected = Vec3(tt[third_view]*triangulated_homg).hnormalized();
    apply_intrinsics(K, pxreprojected.data(), pxreprojected.data());
    // The above two lines do K*[R|T]
    // to measure the error in pixels.
    // TODO(gabriel) Triple-check ACRANSAC probably does not need residuals in pixels
     
    Vec2 pxmeasured = pxbearing.col(third_view);
    // cout << "error " << (reprojected - measured).squaredNorm() << "\n";
    // cout << "triang " <<triangulated_homg <<"\n";
    std::cerr << "TRIFOCAL LOG: Finished Error()\n";
    return (pxreprojected-pxmeasured).squaredNorm();
  }
}

} // namespace trifocal3pt
