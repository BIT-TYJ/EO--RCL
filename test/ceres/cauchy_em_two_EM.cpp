#include <ceres/ceres.h>
#include <iostream>
#include <sophus/se3.hpp>
#include <time.h>

#include "local_parameterization_se3.hpp" 

#include "glog/logging.h"
#include <vector> 
#include <fstream>
#include <Eigen/Core>

#include <iomanip> 
#include <algorithm> 

// #define NUM_PAIRS 200
// #define NUM_PAIRS_NEEDED 50
// #define NUM_LOOPS_NEEDED 1770
// #define NUM_CAMERAS 1770

#define SIGMA 0.5
#define SIGMA_two_EM 0.2
#define NUM_ITERATION_FIRST_EM 1
#define NUM_ITERATION_SECOND_EM 5
#define PI 3.1415926535897
#define Median_inlier_loss   3                //调节公式(13)中的圆符号  change round signal


//空间一致性 ,高斯函数_的参数设置 spatial consistency  gaussian function   //     l_loop:  l_odometry_ave*0.1------------l_odometry_ave*0.5     l_point:  point_num*0.1------------point_num*0.5
//点对和约束的高斯函数  自变量在各比例值时的  各高斯函数值相同      
//L_vector[id1][id2](l_odometry_ave*0.1) = 0.028;   L_vector[id1][id2](l_odometry_ave*0.2) = 0.135  ;   
//L_vector[id1][id2](l_odometry_ave*0.3) = 0.41;  L_vector[id1][id2](l_odometry_ave*0.4) = 0.8;   L_vector[id1][id2](l_odometry_ave*0.5) = 1  ; 
//点对高斯函数的参数设置  point pair:gaussian function parameters 
#define sigma_point_pair   30           //0.15*200 
#define  upper_threshold_point_pair  100   ///0.5*200
#define  lower_threshold_point_pair  20   ///0.1*200

//约束高斯函数的参数设置 ：只考虑比例，不代入具体的数值     constraint : guassian function parameters
#define sigma_constraint   0.15          //0.15*N^-O
#define  upper_threshold_constraint  0.5   ///0.5*N^-O
#define  lower_threshold_constraint  0.1   ///0.1*N^-O

#define PARETO_ALPHA 1

#define ZIQUAN true
#define NUM_FEATURE_MATCHES_PER_CONSTRAINT 200
#define TRUST_ODOMETRY 0.9

#define  point_num_for_optimize_second  100
#define  point_num_for_optimize_first  40

// typedef std::pair< int, int > IntPair;
typedef Eigen::Matrix< double, 6, 6, Eigen::RowMajor > InformationMatrix;
typedef Eigen::Matrix< double, 6, 1> Vector6d;
typedef Eigen::Matrix< int, Eigen::Dynamic, 3, Eigen::RowMajor > Semantic_lable_read;   //元素为：语义一致的标签位、语义标签1、语义标签2  the flag if semantic labels are the same  label_1  and label_2
typedef Eigen::Matrix< double, Eigen::Dynamic, 6, Eigen::RowMajor > PairMatrix;

using ceres::AutoDiffCostFunction;
using ceres::CostFunction;
using ceres::Problem;
using ceres::Solver;
using ceres::Solve;

/*
  ZHOU's convension
*/
// std::vector<std::vector<double>>  semantic_p;    //保存语义混淆矩阵


  // void LoadFromFile_semantic_p(const char*  filename ) {
  //   semantic_p.clear();
  //   semantic_p.resize(24);
  //   for(int i=0;i<24;i++)
  //   {
  //     semantic_p[i].resize(24);
  //   }
  //   FILE * f = fopen( filename, "r" );
  //   int i=0;
  //   if ( f != NULL ) {
  //     char buffer[1024];
  //     while ( fgets( buffer, 1024, f ) != NULL &&i<24) 
  //     {
  //       if ( strlen( buffer ) > 0 && buffer[ 0 ] != '#' ) 
  //       {
  //         sscanf( buffer, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",  
  //          &semantic_p[i][0],&semantic_p[i][1],&semantic_p[i][2], &semantic_p[i][3], &semantic_p[i][4], &semantic_p[i][5],
  //         &semantic_p[i][6],&semantic_p[i][7],&semantic_p[i][8], &semantic_p[i][9], &semantic_p[i][10], &semantic_p[i][11],
  //         &semantic_p[i][12],&semantic_p[i][13],&semantic_p[i][14], &semantic_p[i][15], &semantic_p[i][16], &semantic_p[i][17],
  //         &semantic_p[i][18],&semantic_p[i][19],&semantic_p[i][20], &semantic_p[i][21], &semantic_p[i][22], &semantic_p[i][23]);
  //       }
  //       i++;
  //     }
  //     fclose( f );
  //   }
  // }

struct FramedMatches {
  int id1_;
  int id2_;
  int frame_;
  int old_correspondence_num_;
  int new_correspondence_num_;
  float ratio_;
  Eigen::Matrix4d transformation_;
  std::vector< std::pair<Eigen::Vector3d, Eigen::Vector3d> > pairs_;
  std::vector<int> semantic_label;          //存储每个点对 语义是否对应的标志位  save the flag if semantic labels are the same for each point pair
  std::vector<int> label_one_;         //每对对应点第一个点的语义标签值   the semantic label of the first point for each point pair
  std::vector<int> label_two_;        //每对对应点第二个点的语义标签值  the semantic label of the second point for each point pair
  FramedMatches( int id1, int id2, int f, int old_cor,int new_cor,float ratio,Eigen::Matrix4d trans, PairMatrix pairs,Semantic_lable_read sem)
  : id1_(id1), id2_(id2), frame_(f), old_correspondence_num_(old_cor),new_correspondence_num_(new_cor),ratio_(ratio),transformation_(trans)
  {
    int num_pairs = pairs.rows();
    pairs_.resize(num_pairs);
    semantic_label.resize(num_pairs);
    label_one_.resize(num_pairs);
    label_two_.resize(num_pairs);
    for (int i = 0; i < num_pairs;  i++) {
      pairs_[i] = std::make_pair(Eigen::Vector3d(pairs(i,0), pairs(i,1), pairs(i,2)), 
                                 Eigen::Vector3d(pairs(i,3), pairs(i,4), pairs(i,5)));
      semantic_label[i]=sem(i,0);
      label_one_[i]=sem(i,1);
      label_two_[i]=sem(i,2);
    }
  }
};

struct PCLMatches {
  std::vector< FramedMatches > data_;

  void LoadFromFile(const char* filename) {// load files of odometry and loop constraints
    data_.clear();
    int id1, id2, frames=22, old_correspondence_num, new_correspondence_num;
    float ratio;
    Eigen::Matrix4d trans;
    PairMatrix pairs;
    Semantic_lable_read sem;
    FILE * f = fopen( filename, "r" );
    if ( f != NULL ) {
      char buffer[1024];
      // int counter = 0;
      // while ( (num < 0 || counter <= num) && fgets( buffer, 1024, f ) != NULL ) {
      while ( fgets( buffer, 1024, f ) != NULL ) {
        if ( strlen( buffer ) > 0 && buffer[ 0 ] != '#' ) {
          // if (ZIQUAN) {       ratio=new_correspondence_num/old_correspondence_num
            sscanf( buffer, "%d %d %d %d %f", &id1, &id2, &old_correspondence_num, &new_correspondence_num, &ratio);
          // } else {
            // sscanf( buffer, "%d %d %d %d %d %f", &id1, &id2, &frames, &count_in, &count_out, &ratio);
          // }
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf", &trans(0,0), &trans(0,1), &trans(0,2), &trans(0,3) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf", &trans(1,0), &trans(1,1), &trans(1,2), &trans(1,3) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf", &trans(2,0), &trans(2,1), &trans(2,2), &trans(2,3) );
          trans(3,0) = 0; trans(3,1) = 0; trans(3,2) = 0; trans(3,3) = 1;
          // if (ZIQUAN) {
          //   count_in = 200;
          // }
          pairs.resize(old_correspondence_num, 6);
          sem.resize(old_correspondence_num, 3);
          for (int i = 0; i < old_correspondence_num; i++) {
            fgets( buffer, 1024, f );
            sscanf( buffer, "%lf %lf %lf %lf %lf %lf %d %d %d", &pairs(i,0), &pairs(i,1), &pairs(i,2), &pairs(i,3), \
                                                           &pairs(i,4), &pairs(i,5), &sem(i,0),&sem(i,1),&sem(i,2));
          }
          // if (ratio > min_ratio) {
            data_.push_back( FramedMatches( id1, id2, frames, old_correspondence_num,new_correspondence_num,ratio,trans, pairs,sem) );
            // counter ++ ;
          // }
          // std::cout << id1 << " and " << id2  << std::endl;
        }
      }
      fclose( f );
    }
  }
};

struct FramedTransformation {
  int id1_;
  int id2_;
  int frame_;
  Eigen::Matrix4d transformation_;      // pose
  Sophus::SE3d transformation_se3_;     // useful for both pose and link
  // std::vector< std::pair<Eigen::Vector3d, Eigen::Vector3d> > pairs_;

  FramedTransformation( int id1, int id2, int f, Eigen::Matrix4d t)
    : id1_( id1 ), id2_( id2 ), frame_( f ), transformation_( t ) 
  {
    Eigen::Quaterniond q;
    q = t.block<3,3>(0,0);
    transformation_se3_ = Sophus::SE3d(q, Sophus::SE3d::Point(t(0,3), t(1,3), t(2,3)));
    // pairs_.resize(NUM_PAIRS_NEEDED);
    // for (int i = 0; i < NUM_PAIRS_NEEDED; i++) {
    //   pairs_[i] = std::make_pair(Eigen::Vector3d(pairs(i,0), pairs(i,1), pairs(i,2)), 
    //                              Eigen::Vector3d(pairs(i,3), pairs(i,4), pairs(i,5)));
    // }
  }

  // only use this constructer to check for convergence
  FramedTransformation(int id1, int id2, int f, Sophus::SE3d t)
    : id1_( id1 ), id2_( id2 ), frame_( f ), transformation_se3_( t ) 
    {
      transformation_ = t.matrix();
    }
};

struct PCLTrajectory {
  std::vector< FramedTransformation > data_;
  int index_;

  void LoadFromFile(const char* filename ) {   // load init poses file
    data_.clear();
    index_ = 0;
    int id1, id2, frame;
    Eigen::Matrix4d trans;
    FILE * f = fopen( filename, "r" );
    if ( f != NULL ) {
      char buffer[1024];
      while ( fgets( buffer, 1024, f ) != NULL ) {
        if ( strlen( buffer ) > 0 && buffer[ 0 ] != '#' ) {
          sscanf( buffer, "%d %d %d", &id1, &id2, &frame);
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf", &trans(0,0), &trans(0,1), &trans(0,2), &trans(0,3) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf", &trans(1,0), &trans(1,1), &trans(1,2), &trans(1,3) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf", &trans(2,0), &trans(2,1), &trans(2,2), &trans(2,3) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf", &trans(3,0), &trans(3,1), &trans(3,2), &trans(3,3) );
          data_.push_back( FramedTransformation( id1, id2, frame, trans ) );
          // trans(3,0) = 0; trans(3,1) = 0; trans(3,2) = 0; trans(3,3) = 1;
          // for (int i = 0; i < NUM_PAIRS; i++) {
          //   fgets( buffer, 1024, f );
          //   sscanf( buffer, "%lf %lf %lf %lf %lf %lf %lf", &pairs(i,0), &pairs(i,1), &pairs(i,2), &pairs(i,3), 
          //                                                  &pairs(i,4), &pairs(i,5), &pairs(i,6));
          // }
          // if (ratio > min_ratio) {
          //   data_.push_back( FramedTransformation( id1, id2, 1770, trans ) );
          //   counter ++ ;
          // }
          // std::cout << id1 << "\t" << id2  << std::endl;
        }
      }
      // std::cout << "pose num is:"<<data_.size()  << std::endl;
      fclose( f );
    }
  }

  void SaveToFile(const char* filename ) {   // save output poses files
    FILE * f = fopen( filename, "w" );
    for ( int i = 0; i < ( int )data_.size(); i++ ) {
      Sophus::SE3d trans_se3 = data_[ i ].transformation_se3_;
      Eigen::Matrix4d trans = trans_se3.matrix();
      fprintf( f, "%d\t%d\t%d\n", data_[ i ].id1_, data_[ i ].id2_, data_[ i ].frame_ );
      fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(0,0), trans(0,1), trans(0,2), trans(0,3) );
      fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(1,0), trans(1,1), trans(1,2), trans(1,3) );
      fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(2,0), trans(2,1), trans(2,2), trans(2,3) );
      fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(3,0), trans(3,1), trans(3,2), trans(3,3) );
    }
    fclose( f );
  }

};

struct FramedInformation {
  int id1_;
  int id2_;
  int frame_;
  InformationMatrix information_;
  FramedInformation( int id1, int id2, int f, InformationMatrix t )
    : id1_( id1 ), id2_( id2 ), frame_( f ), information_( t ) 
  {}
};

struct PCLInformation {
  std::vector< FramedInformation > data_;

  void LoadFromFile(const char*  filename ) {  // load information matrix files ,not used in cauchy 
    data_.clear();
    int id1, id2, frame;
    InformationMatrix info;
    FILE * f = fopen( filename, "r" );
    if ( f != NULL ) {
      char buffer[1024];
      while ( fgets( buffer, 1024, f ) != NULL ) {
        if ( strlen( buffer ) > 0 && buffer[ 0 ] != '#' ) {
          sscanf( buffer, "%d %d %d", &id1, &id2, &frame);
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf %lf %lf", &info(0,0), &info(0,1), &info(0,2), &info(0,3), &info(0,4), &info(0,5) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf %lf %lf", &info(1,0), &info(1,1), &info(1,2), &info(1,3), &info(1,4), &info(1,5) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf %lf %lf", &info(2,0), &info(2,1), &info(2,2), &info(2,3), &info(2,4), &info(2,5) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf %lf %lf", &info(3,0), &info(3,1), &info(3,2), &info(3,3), &info(3,4), &info(3,5) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf %lf %lf", &info(4,0), &info(4,1), &info(4,2), &info(4,3), &info(4,4), &info(4,5) );
          fgets( buffer, 1024, f );
          sscanf( buffer, "%lf %lf %lf %lf %lf %lf", &info(5,0), &info(5,1), &info(5,2), &info(5,3), &info(5,4), &info(5,5) );
          data_.push_back( FramedInformation( id1, id2, frame, info ) );
          // std::cout << id1 << "\t" << id2  << std::endl;
        }
      }
      fclose( f );
    }
  }
};


// Eigen's ostream operator is not compatible with ceres::Jet types.
// In particular, Eigen assumes that the scalar type (here Jet<T,N>) can be
// casted to an arithmetic type, which is not true for ceres::Jet.
// Unfortunatly, the ceres::Jet class does not define a conversion
// operator (http://en.cppreference.com/w/cpp/language/cast_operator).
//
// This workaround creates a template specilization for Eigen's cast_impl,
// when casting from a ceres::Jet type. It relies on Eigen's internal API and
// might break with future versions of Eigen.
namespace Eigen {
namespace internal {

template <class T, int N, typename NewType>
struct cast_impl<ceres::Jet<T, N>, NewType> {
  EIGEN_DEVICE_FUNC
  static inline NewType run(ceres::Jet<T, N> const& x) {
    return static_cast<NewType>(x.a);
  }
};

}  // namespace internal
}  // namespace Eigen

struct TestSE3CostFunctor {
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  TestSE3CostFunctor(Sophus::SE3d T_aw) : T_aw(T_aw) {}

  template <class T>
  bool operator()(T const* const sT_wa, T* sResiduals) const {
    Eigen::Map<Sophus::SE3<T> const> const T_wa(sT_wa);
    Eigen::Map<Eigen::Matrix<T, 6, 1> > residuals(sResiduals);

    // We are able to mix Sophus types with doubles and Jet types withou needing
    // to cast to T.
    residuals = (T_aw * T_wa).log();
    // std::cout << residuals << std::endl;

    // Reverse order of multiplication. This forces the compiler to verify that
    // (Jet, double) and (double, Jet) SE3 multiplication work correctly.
    // residuals = (T_wa * T_aw).log();

    // Finally, ensure that Jet-to-Jet multiplication works.
    // residuals = (T_wa * T_aw.cast<T>()).log();
    return true;
  }

  Sophus::SE3d T_aw;
};

struct TestPointCostFunctor {
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  TestPointCostFunctor(Sophus::SE3d T_aw, Eigen::Vector3d point_a)
      : T_aw(T_aw), point_a(point_a) {}

  template <class T>
  bool operator()(T const* const sT_wa, T const* const spoint_b,
                  T* sResiduals) const {
    using Vector3T = Eigen::Matrix<T, 3, 1>;
    Eigen::Map<Sophus::SE3<T> const> const T_wa(sT_wa);
    Eigen::Map<Vector3T const> point_b(spoint_b);
    Eigen::Map<Vector3T> residuals(sResiduals);

    // Multiply SE3d by Jet Vector3.
    Vector3T point_b_prime = T_aw * point_b;
    // Ensure Jet SE3 multiplication with Jet Vector3.
    // point_b_prime = T_aw.cast<T>() * point_b;

    // Multiply Jet SE3 with Vector3d.
    Vector3T point_a_prime = T_wa * point_a;
    // Ensure Jet SE3 multiplication with Jet Vector3.
    // point_a_prime = T_wa * point_a.cast<T>();

    residuals = point_b_prime - point_a_prime;
    return true;
  }

  Sophus::SE3d T_aw;
  Eigen::Vector3d point_a;
};

//  GaussianFunctor is not used in this work.
struct GaussianFunctor {
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  GaussianFunctor(Sophus::SE3d trans_next_to_current, InformationMatrix info, int id1, int id2) 
  : trans_next_to_current_(trans_next_to_current), info_(info), id1_(id1), id2_(id2) {}

  template <typename T> 
  bool operator()(T const* const sT_current, T const* const sT_next, 
                  T* residual) const {
    // using Vector3T = Eigen::Matrix<T, 3, 1>;
    Eigen::Map<Sophus::SE3<T> const> const T_current(sT_current);
    Eigen::Map<Sophus::SE3<T> const> const T_next(sT_next);
    // Eigen::Map<Vector3T> residuals(sResiduals);

    // Eigen::Matrix<T,4,4> M_xi = (trans_next_to_current_ * T_next.inverse() * T_current).matrix();
    Sophus::SE3<T> xi_7T = trans_next_to_current_ 
                          * T_next.inverse() 
                          * T_current;

    Eigen::Matrix<T,6,1> xi;
    // xi << M_xi(0,3), M_xi(1,3), M_xi(2,3), q.x(), q.y(), q.z();
    xi << xi_7T.data()[4], xi_7T.data()[5], xi_7T.data()[6], xi_7T.data()[0], xi_7T.data()[1], xi_7T.data()[2];

    Eigen::Matrix<T,1,1> sq_error = xi.transpose() * info_ * xi;
    // double sq_error = (xi.transpose() * info_ * xi)[0];
    
    residual[0] = T( sqrt(sq_error(0,0)) );
    // residual[0] = T( sqrt(sq_error(0,0) / T(2.)) / T(SIGMA) );

    // std::cout << id1_ << ", " << id2_ << " : " << residual[0] << std::endl;
    return true;
  }

private:
  const Sophus::SE3d trans_next_to_current_;
  const InformationMatrix info_;
  const int id1_, id2_;
};

// define CauchyFunctor 
struct CauchyFunctor {
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  CauchyFunctor(Eigen::Vector3d point_p, Eigen::Vector3d point_q, 
                int id1, int id2) 
  : point_p_(point_p), point_q_(point_q),
    id1_(id1), id2_(id2) {}

  template <typename T> 
  bool operator()(T const* const sT_current, T const* const sT_next, 
                  T* sResiduals) const {
    // using Vector3T = Eigen::Matrix<T, 3, 1>;
    Eigen::Map<Sophus::SE3<T> const> const T_current(sT_current);
    Eigen::Map<Sophus::SE3<T> const> const T_next(sT_next);
    Eigen::Map<Eigen::Matrix<T, 3, 1> > residuals(sResiduals);

    residuals = (T_current * point_p_ - T_next * point_q_);

    return true;
  }

private:
  const Eigen::Vector3d point_p_, point_q_;
  const int id1_, id2_;
};

class R2EM_CauchyUniform {
public:
  // lambda  : converge for first EM
  double lambda_;
  //  lambda_second_ : converge for second EM
  double  lambda_second_;
  double U;
  // double u_;
  Eigen::Matrix<double, 6, 6> covarance_;

  // read from file
  // PCLTrajectory odometry_log_;
  // PCLInformation odometry_info_;
  PCLMatches odometry_txt_;
  // PCLTrajectory loop_log_;
  PCLMatches loop_txt_;
  // PCLInformation loop_info_;

  // blocks for ceres
  PCLTrajectory camera_poses_;
  std::vector< std::vector<double> > L_;     //单个loop正确的概率值(语义)    correct probability of a loop constraint
  double sum_L_;   //用于检测 first EM 收敛性   for test convergence of first EM
  std::vector< std::vector<double> > L_P_;    //所有odometry、loop中每个点对正确的概率   correct probability of a point pair in a constraint
  double sum_L_P_;   //用于检测 second EM收敛性   for test convergence of second EM
  std::vector< std::vector<float> > L_vector;     //单个loop正确的概率  judge whether a loop is correct from SCDM
  std::vector< std::vector<float> > vector_true;         //根据向量对是否相同：所有odometry、loop中每个点对正确概率  SCDM


  // convergence condition  for first EM
  double last_lambda_;
  PCLTrajectory last_camera_poses_;
  Eigen::Matrix<double, 6, 6> last_covarance_;
  // convergence condition  for second EM
  double last_lambda_second_;

  R2EM_CauchyUniform(double lambda)
  : lambda_(lambda){
    last_lambda_ = -1.;
    lambda_second_=0.99;
    last_lambda_second_ = -1.;
  }
  ~R2EM_CauchyUniform() {}

  // void LoadOdometryLog(const char* filename) {
  //   odometry_log_.LoadFromFile(filename);
  // }

  // void LoadOdometryInfo(const char* filename) {
  //   odometry_info_.LoadFromFile(filename);
  // }

  void LoadOdometryTxt(const char* filename) {
    odometry_txt_.LoadFromFile(filename);//, -1);
  }

  void LoadLoopTxt(const char* filename) {
    loop_txt_.LoadFromFile(filename);//, -1);
  }

  // void LoadLoopInfo(const char* filename) {
  //   loop_info_.LoadFromFile(filename);
  // }

  void InitCameraPosesWithZhou(const char* filename) {
    camera_poses_.LoadFromFile(filename);
    L_.resize(NumPoses());
    for (int i = 0; i < NumPoses(); i++) {
      L_[i].resize(NumPoses());
    }
  }

  void InitCameraPoses() { // init poses of submaps
    camera_poses_.data_.clear();
    camera_poses_.index_ = 0;
    Eigen::Matrix4d pose = Eigen::Matrix4d::Identity();
    camera_poses_.data_.push_back( FramedTransformation( 0, 0, 1, pose ) );

    for( std::vector< FramedMatches >::iterator it = odometry_txt_.data_.begin();
      it != odometry_txt_.data_.end(); it ++)
    {      
      pose = pose * it->transformation_; 
      camera_poses_.data_.push_back( FramedTransformation( it->id2_, it->id2_, it->id2_ + 1, pose ) );
    }
    // std::cout << "odometry_log_.data_.size(): " << odometry_log_.data_.size() << std::endl;

    L_.resize(NumPoses());
    for (int i = 0; i < NumPoses(); i++) {
      L_[i].resize(NumPoses());
    }

  }; 

  void SaveCameraPoses(const char* filename) {
    camera_poses_.SaveToFile(filename);
  }

  void EstimateLAMBDA() {                                             //estimate  cauchy loss for odometry and loop constraints

    std::cout << "In EstimateLAMBDA : " << NumOdometryConstraints() << std::endl;
    std::vector<double> average_loss(NumOdometryConstraints());
    std::vector<double> total_loss(NumOdometryConstraints());
    for (int i = 0; i < NumOdometryConstraints(); i ++) {

      FramedMatches match = odometry_txt_.data_[i];
      // FramedInformation info = odometry_info_.data_[i];

      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;

      // std::cout << "In EstimateLAMBDA : " << i << "th OdometryConstraint " << id1 << " with " << id2 << std::endl;

      Eigen::Quaterniond q;
      q = match.transformation_.block<3,3>(0,0);
      Sophus::SE3d transformation_se3_ = Sophus::SE3d(q, Sophus::SE3d::Point(match.transformation_(0,3), match.transformation_(1,3), match.transformation_(2,3)));
      
      // compute the sum of cauchy loss
      double sum_cauchy_loss = 0.;
      for (std::size_t j = 0; j < point_num_for_optimize_first; j++) {
        Eigen::Vector3d vec_d = camera_poses_.data_[id1].transformation_se3_ * match.pairs_[j].first 
                              - camera_poses_.data_[id2].transformation_se3_ * match.pairs_[j].second;
        sum_cauchy_loss += log (1. + vec_d.squaredNorm() / (SIGMA * SIGMA));
        // std::cout << j << "/" << loop_log_.data_.size() << " : " << sum_cauchy_loss << std::endl;
      }
      assert(point_num_for_optimize_first > 0);
      average_loss[i] = sum_cauchy_loss / point_num_for_optimize_first;
      total_loss[i] = sum_cauchy_loss;
      // std::cout << "point_num_for_optimize_odometry is " << point_num_for_optimize_odometry << '\n';
      // std::cout << "sum_cauchy_loss " << sum_cauchy_loss << '\n';
      // std::cout << "average_loss[i] " << average_loss[i] << '\n';
    }

    for (int i = 0; i < NumOdometryConstraints(); i ++) {
      FramedMatches match = odometry_txt_.data_[i];
      // FramedInformation info = odometry_info_.data_[i];

      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;

      // double expterm = exp( - log(U) + (1. + PARETO_ALPHA) * log(average_loss[i]));
      double expterm = exp( - log(U) + (1. + PARETO_ALPHA) * (average_loss[i]));
      // L_[id1][id2] = 1./ (1. + expterm);
      // std::cout << "(" << std::setw(3) << id1 << "," << std::setw(3) << id2 << ") : " 
      //           << std::setw(15) << average_loss[i] << "(" <<  total_loss[i] << ")"
      //           << std::setw(15) << average_loss[i] * average_loss[i]
      //           << std::setw(15) << expterm <<  " --> "
      //           << std::setw(15) << 1./ (1. + expterm)
      //           << std::setw(15) << point_num_for_optimize_odometry
      //           // << std::setw(15) << U_big
      //           << std::endl;
    }
    std::nth_element(average_loss.begin(), average_loss.begin() + average_loss.size()/2, average_loss.end());
    std::nth_element(total_loss.begin(), total_loss.begin() + total_loss.size()/2, total_loss.end());
    // double median_inlier_loss = average_loss[average_loss.size()/2];
    
    double median_inlier_loss = exp(2 * average_loss[average_loss.size()/2]);
    std::cout << "average_loss[average_loss.size()/2] " << average_loss[average_loss.size()/2] << '\n';

    // double median_total_loss = total_loss[average_loss.size()/2];
    std::cout << "The median is " << median_inlier_loss << '\n';
    // U =  TRUST_ODOMETRY / (1-TRUST_ODOMETRY) * median_inlier_loss * median_inlier_loss;
    U =  TRUST_ODOMETRY / (1-TRUST_ODOMETRY) * median_inlier_loss;
    // double U_big =  TRUST_ODOMETRY / (1-TRUST_ODOMETRY) * median_total_loss * median_total_loss;

  }

  void SaveLinks(const char* filename) {     //save correct constraints
    FILE * f = fopen( filename, "w" );

    for ( int i = 0; i < NumOdometryConstraints(); i++ ) {
      Eigen::Matrix4d trans = odometry_txt_.data_[ i ].transformation_;
      // Eigen::Matrix4d trans = trans_se3.matrix();
      fprintf( f, "%d\t%d\t%d\n", odometry_txt_.data_[ i ].id1_, odometry_txt_.data_[ i ].id2_, odometry_txt_.data_[ i ].frame_ );
      fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(0,0), trans(0,1), trans(0,2), trans(0,3) );
      fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(1,0), trans(1,1), trans(1,2), trans(1,3) );
      fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(2,0), trans(2,1), trans(2,2), trans(2,3) );
      fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(3,0), trans(3,1), trans(3,2), trans(3,3) );
    }

    for (int i = 0; i < NumLoopClosureConstraints(); i++ ) {
      Eigen::Matrix4d trans = loop_txt_.data_[ i ].transformation_;
      int id1 = loop_txt_.data_[ i ].id1_;
      int id2 = loop_txt_.data_[ i ].id2_;
      assert(id1 < id2);
      if (id1 != id2 - 1 && L_[id1][id2] > 0.8 * TRUST_ODOMETRY) {
        fprintf( f, "%d\t%d\t%d\n", loop_txt_.data_[ i ].id1_, loop_txt_.data_[ i ].id2_, loop_txt_.data_[ i ].frame_ );
        fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(0,0), trans(0,1), trans(0,2), trans(0,3) );
        fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(1,0), trans(1,1), trans(1,2), trans(1,3) );
        fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(2,0), trans(2,1), trans(2,2), trans(2,3) );
        fprintf( f, "%.8f %.8f %.8f %.8f\n", trans(3,0), trans(3,1), trans(3,2), trans(3,3) );
      }
    }
    fclose( f );
  }

// Expectation steps
  void Expectation(int iteration_num) {
   
    sum_L_ = 0.;
    std::vector<int> healthy_counter(20);
    for (int ii = 0; ii < 20; ii ++) {
      healthy_counter[ii] = 0;
    }

  int maxnum_loop_corres=0;
  // //寻找所有loop中最大对应点对数
  // for (int i = 0; i < NumLoopClosureConstraints(); i ++) 
  // {
  //   FramedMatches match = loop_txt_.data_[i];
  //   if(match.old_correspondence_num_>maxnum_loop_corres)
  //   {
  //     maxnum_loop_corres = match.old_correspondence_num_;
  //   }  
  // }

    for (int i = 0; i < NumLoopClosureConstraints(); i ++) {

      FramedMatches match = loop_txt_.data_[i];
      // FramedTransformation trans = loop_log_.data_[i];
      // FramedInformation info = loop_info_.data_[i];

      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;

      if(L_vector[id1][id2]==0) continue;   //if the inlier probability of a  loop is 0 by SCDM,  continue
      // std::cout << id1 << " ? " << id2 <<std::endl;

      // compute the sum of cauchy loss
      double sum_cauchy_loss = 0.;
      for (std::size_t j = 0; j < point_num_for_optimize_first; j++) {
        
          if(vector_true[i][j]==0 ) continue;     //if the inlier probability of a  point pair is 0 by SCDM,  continue

        Eigen::Vector3d vec_d = camera_poses_.data_[id1].transformation_se3_ * match.pairs_[j].first 
                              -camera_poses_.data_[id2].transformation_se3_ * match.pairs_[j].second;
        if(1)
        {
          sum_cauchy_loss += log (1. + vec_d.squaredNorm() / (SIGMA * SIGMA));
        }
        else
        {
          // sum_cauchy_loss += log (1. + vec_d.squaredNorm() / (SIGMA * SIGMA));
           sum_cauchy_loss+=0;
        }
        // std::cout << j << "/" << loop_log_.data_.size() << " : " << sum_cauchy_loss << std::endl;
      }
      if (point_num_for_optimize_first > 0) {
        sum_cauchy_loss /= point_num_for_optimize_first;
      }

      // double expterm = exp( - log(U) + (1. + PARETO_ALPHA) * log(sum_cauchy_loss));
      // L_[id1][id2] = 1./ (1. + expterm);
      
      // U =  TRUST_ODOMETRY / (1-TRUST_ODOMETRY) * Median_inlier_loss;
      L_[id1][id2] = U / (U + exp(2 * sum_cauchy_loss));


        // std::cout << id1 <<" " << id2 << std::endl;
        // std::cout << log (1. + (camera_poses_.data_[id1].transformation_se3_ * match.pairs_[0].first 
        //                       -camera_poses_.data_[id2].transformation_se3_ * match.pairs_[0].second).squaredNorm() / (SIGMA * SIGMA))*L_[id1][id2] << std::endl;
      
      //correct P of loops =1  for the first E step,and consider semantic ratio
       if(iteration_num ==0) 
       {
        L_[id1][id2]=1;
        L_[id1][id2]=L_[id1][id2]*exp(-(match.ratio_-1)*(match.ratio_-1)/(2*0.1));
       }                                                                                                        

      // std::cout << "(" << std::setw(3) << id1 << "," << std::setw(3) << id2 << ") : " 
      //           << std::setw(15) << sum_cauchy_loss
      //           << std::setw(15) << sum_cauchy_loss * sum_cauchy_loss
      //           // << std::setw(15) << expterm <<  " --> "
      //           << std::setw(15) << exp(2 * sum_cauchy_loss / point_num_for_optimize_odometry) <<  " --> "
      //           << std::setw(15) <<  L_[id1][id2]
      //           << std::endl;

      for (int ii = 0; ii < 20; ii ++) {
        if (L_[id1][id2] > 0.05 * ii && L_[id1][id2] <= 0.05 * (ii+1)){
          healthy_counter[ii] ++;
        } 
      }
      
      sum_L_ += L_[id1][id2];
    }

    std::cout << "\t\tsum_L_ is " << sum_L_ << std::endl;
    std::cout << "\t\tHealthy is ";
    for (int ii = 0; ii < 20; ii ++) {
       std::cout << "\t(" << (0.05 * ii) << ")\t" << healthy_counter[ii] ;
    }
    std::cout << "\t(1.0)\t" << std::endl;
    std::cout << "\t\tTotal is " << NumLoopClosureConstraints() << std::endl;
    std::cout << "\t\testimated LAMBDA is " << U << std::endl;
  }

  //first Maximization step
  void Maximization() {
    last_lambda_ = lambda_;
    last_camera_poses_.index_ = camera_poses_.index_;
    last_camera_poses_.data_.clear();
    for (int i = 0; i < NumPoses(); i++) {
      last_camera_poses_.data_.push_back( 
        FramedTransformation (camera_poses_.data_[i].id1_, 
                              camera_poses_.data_[i].id2_, 
                              camera_poses_.data_[i].frame_,
                              camera_poses_.data_[i].transformation_se3_));
    }
    camera_poses_.index_ ++;
    // last_covarance_ = covarance_;

    // Maximize lambda
    lambda_ = sum_L_ / NumLoopClosureConstraints();

    // Maximize pose
    // Build the problem.
    ceres::Problem problem;

    // Specify local update rule for our parameter
    for (std::vector< FramedTransformation >::iterator it = camera_poses_.data_.begin(); 
         it != camera_poses_.data_.end(); it++ ) {
      problem.AddParameterBlock(it->transformation_se3_.data(), Sophus::SE3d::num_parameters,
                                new Sophus::test::LocalParameterizationSE3);
    }

    // Create and add cost functions. Derivatives will be evaluated via
    // automatic differentiation                               
    int  diuqi=0 ;
    for (int i = 0; i < NumOdometryConstraints(); i++) 
    {
          
      // FramedTransformation trans = odometry_log_.data_[i];
      // FramedInformation info = odometry_info_.data_[i];
      
      // assert(trans.id1_ == info.id1_ && trans.id2_ == info.id2_ && trans.id1_ < trans.id2_);
      // int id1 = info.id1_;
      // int id2 = info.id2_;

      // if (info.information_(0,0) <= 1)
      //   continue;

      // ceres::CostFunction* cost_odometry =
      //     new ceres::AutoDiffCostFunction<GaussianFunctor, 1,
      //                                     Sophus::SE3d::num_parameters,
      //                                     Sophus::SE3d::num_parameters>(
      //         new GaussianFunctor(trans.transformation_se3_, info.information_, id1, id2));
      // problem.AddResidualBlock(cost_odometry, NULL, 
      //                          camera_poses_.data_[id1].transformation_se3_.data(), 
      //                          camera_poses_.data_[id2].transformation_se3_.data());

      FramedMatches match = odometry_txt_.data_[i];
      
      assert(match.id1_ < match.id2_);
      int id1 = match.id1_; 
      int id2 = match.id2_;
        if(1)         ///////////////////////////////////////////////////////////
        { 
          diuqi=0 ;
          for (std::size_t j = 0; j < point_num_for_optimize_first; j++) 
          {
        
            if(vector_true[i+NumLoopClosureConstraints()][j]!=0)        
            {
              // std::cout << match.pairs_[j].first[0] << "  " << match.pairs_[j].first[1] << "  " <<match.pairs_[j].first[2] << " second: " <<
              //  match.pairs_[j].second[0] << "  " << match.pairs_[j].second[1] << "  " <<match.pairs_[j].second[2] << std::endl;
            ceres::CostFunction* cost_odometry =
                new ceres::AutoDiffCostFunction<CauchyFunctor, 3,
                                              Sophus::SE3d::num_parameters,
                                              Sophus::SE3d::num_parameters>(
                    new CauchyFunctor(match.pairs_[j].first, match.pairs_[j].second, 
                                      id1, id2));
            problem.AddResidualBlock(cost_odometry, 
                                    new ceres::CauchyLoss(SIGMA),
                                    camera_poses_.data_[id1].transformation_se3_.data(), 
                                    camera_poses_.data_[id2].transformation_se3_.data());                        
            }
          }
          // std::cout << id1 <<"is: "<<diuqi <<std::endl;
        }

    }

    for (int i = 0; i < NumLoopClosureConstraints(); i++) 
    {

      // std::cout << "Maximization + loops " << (i+1) << " / " << NumLoopClosureConstraints() <<  std::endl;
      // FramedTransformation trans = loop_log_.data_[i];
      // FramedInformation info = loop_info_.data_[i];
      FramedMatches match = loop_txt_.data_[i];
      
      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;
       
        if(L_vector[id1][id2]==0) continue; //不正确的loop不参与优化   if the inlier probability of a loop is 0, continue

        for (std::size_t j = 0; j < point_num_for_optimize_first ; j++) 
        {
          if(match.semantic_label[j]==1 && vector_true[i][j]!=0)
          {
          // {
          ceres::CostFunction* cost_loop =
              new ceres::AutoDiffCostFunction<CauchyFunctor, 3,
                                              Sophus::SE3d::num_parameters,
                                              Sophus::SE3d::num_parameters>(
                  new CauchyFunctor(match.pairs_[j].first, match.pairs_[j].second, 
                                    id1, id2));
          //  if (L_[id1][id2] >= TRUST_ODOMETRY * 0.5) {
            //目标函数，每个odometry和loop应该除以 各自特征点对的数量。(cauchy源代码可能因为特征点数量相同没有除)
            problem.AddResidualBlock(cost_loop, 
                                  new ceres::ScaledLoss(new ceres::CauchyLoss(SIGMA), L_vector[id1][id2]*L_[id1][id2], ceres::TAKE_OWNERSHIP),
                                  camera_poses_.data_[id1].transformation_se3_.data(), 
                                  camera_poses_.data_[id2].transformation_se3_.data());
          }

          // if(match.semantic_label[j]==0 && vector_true[i][j]==1)
          // {
          //   int label_one=match.label_one_[j];
          //   int label_two=match.label_two_[j];
          // ceres::CostFunction* cost_loop =
          //     new ceres::AutoDiffCostFunction<CauchyFunctor, 3,
          //                                     Sophus::SE3d::num_parameters,
          //                                     Sophus::SE3d::num_parameters>(
          //         new CauchyFunctor(match.pairs_[j].first, match.pairs_[j].second, 
          //                           id1, id2));
          // //  if (L_[id1][id2] >= TRUST_ODOMETRY * 0.5) {
          //  if (label_one!=100&&label_two!=100) {
          //   problem.AddResidualBlock(cost_loop, 
          //                         new ceres::ScaledLoss(new ceres::CauchyLoss(SIGMA), L_[id1][id2]*semantic_p[label_one][label_two], ceres::TAKE_OWNERSHIP),
          //                         camera_poses_.data_[id1].transformation_se3_.data(), 
          //                         camera_poses_.data_[id2].transformation_se3_.data());
          //  }
          // }

        }
    }
    

    // Set solver options (precision / method)
    ceres::Solver::Options options;
    // options.max_num_iterations = 1000;
    options.gradient_tolerance = 0.01 * Sophus::Constants<double>::epsilon();
    options.function_tolerance = 0.01 * Sophus::Constants<double>::epsilon();
    // options.linear_solver_type = ceres::DENSE_QR;
    options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;
  
    // Solve
    // std::cout << "--------------SOLVING----------------ITERATION " << camera_poses_.index_ << std::endl;
     std::cout << "--------------SOLVING----------------FIRST EM " << std::endl;
    ceres::Solver::Summary summary;
    Solve(options, &problem, &summary);
    std::cout << summary.BriefReport() << std::endl;
    std::cout << "--------------  DONE ----------------FIRST EM " << std::endl;
  }

void Vector()       //SCDM
  {            
      int l_odometry_ave=0;
    for (int i = 0; i < NumOdometryConstraints(); i++) 
    {
      int l_odometry=0;
      // std::cout << "num of correct odometry:  " << i<< std::endl;

      FramedMatches match = odometry_txt_.data_[i];
      
      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;
      // std::cout << id1 << "   " << id2 <<std::endl;

       int  num_vector[match.pairs_.size()]={0};

        if(1)         /////////////////////////////////////////////////////////// // judge for a point pair in each odometry
        {
          for (std::size_t j = 0; j < match.pairs_.size()-1 && j<200; j++) 
          for (std::size_t k = j+1; k < match.pairs_.size() && k<200; k++) 
          {
              
              Eigen::Vector3d vec_d_1 = (match.pairs_[k].first - match.pairs_[j].first);
              Eigen::Vector3d vec_d_2 = (match.pairs_[k].second - match.pairs_[j].second);

              float sqrt_1=sqrt(vec_d_1[0]*vec_d_1[0]+vec_d_1[1]*vec_d_1[1]+vec_d_1[2]*vec_d_1[2]);
              float sqrt_2=sqrt(vec_d_2[0]*vec_d_2[0]+vec_d_2[1]*vec_d_2[1]+vec_d_2[2]*vec_d_2[2]);

              if(abs(sqrt_1-sqrt_2)<0.1)    
              {
                // l_odometry++;
                num_vector[j]++;                 //与一个顶点相容的顶点数量，即相容性矩阵中的一行之和
                num_vector[k]++;
                // vector_true[i+NumLoopClosureConstraints()][j]=1;
                // vector_true[i+NumLoopClosureConstraints()][k]=1;
              }      
          }

          for(int m=0;m<match.pairs_.size();m++)
          {
            // std::cout << num_vector[m] << std::endl;
            if(num_vector[m] > upper_threshold_point_pair)   
            {
              vector_true[i+NumLoopClosureConstraints()][m]=1;         //upper threshold
              // l_odometry++;
            }
            else if(num_vector[m] < lower_threshold_point_pair)
            {
              vector_true[i+NumLoopClosureConstraints()][m]=0;         //lower threshold
            }
            else            //num_vector[m] : lower_threshold_point_pair—— upper_threshold_point_pair
            {
              int pair_number_in_constraint = upper_threshold_point_pair;
              vector_true[i+NumLoopClosureConstraints()][m]=exp( ( -(num_vector[m]-pair_number_in_constraint)*(num_vector[m]-pair_number_in_constraint) ) /  (2*sigma_point_pair*sigma_point_pair) );         //顶点正确与否    
              // std::cout << vector_true[i+NumLoopClosureConstraints()][m] <<std::endl;
            }
            
            l_odometry = l_odometry + num_vector[m];

          }

        }
        l_odometry_ave+=l_odometry/2;       //对称矩阵只考虑上半区的值
        // std:: cout << l_odometry << std::endl;
    }
    l_odometry_ave=l_odometry_ave/NumOdometryConstraints();      //所有odometry中相同向量对数量的平均数   No^-

      std::cout << "l_odometry_ave: " <<l_odometry_ave << std::endl;
    for (int i = 0; i < NumLoopClosureConstraints(); i++) 
    {
      int l_loop=0;
      // std::cout << "num of correct odometry:  " << i<< std::endl;

      FramedMatches match = loop_txt_.data_[i];
       
      int  num_vector[match.pairs_.size()]={0};

      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;
      // std::cout << id1 << "   " << id2 <<std::endl;
        //judge for a point pair in a loop
          for (std::size_t j = 0; j < match.pairs_.size()-1 && j<200; j++) 
          for (std::size_t k = j+1; k < match.pairs_.size() && k<200; k++) 
          {
              
              Eigen::Vector3d vec_d_1 = (match.pairs_[k].first - match.pairs_[j].first);
              Eigen::Vector3d vec_d_2 = (match.pairs_[k].second - match.pairs_[j].second);

              float sqrt_1=sqrt(vec_d_1[0]*vec_d_1[0]+vec_d_1[1]*vec_d_1[1]+vec_d_1[2]*vec_d_1[2]);
              float sqrt_2=sqrt(vec_d_2[0]*vec_d_2[0]+vec_d_2[1]*vec_d_2[1]+vec_d_2[2]*vec_d_2[2]);

              if(abs(sqrt_1-sqrt_2)<0.1)    
              {
                // l_loop++;
                num_vector[j]++;
                num_vector[k]++;

                // vector_true[i][j]=1;
                // vector_true[i][k]=1;
              }

          }
               // judge for a loop
          for(int m=0;m<match.pairs_.size();m++)
          {
              if(num_vector[m] > upper_threshold_point_pair)
            {
              vector_true[i][m]=1;         //顶点正确与否
              // l_loop++;
            }
            else if(num_vector[m] < lower_threshold_point_pair)
            {
              vector_true[i][m]=0;         //顶点正确与否
            }
            else            //num_vector[m] > lower_threshold_point_pair——num_vector[m] > upper_threshold_point_pair
            {
              int pair_number_in_constraint = upper_threshold_point_pair;
              vector_true[i][m]=exp( ( -(num_vector[m]-pair_number_in_constraint)*(num_vector[m]-pair_number_in_constraint) ) /  (2*sigma_point_pair*sigma_point_pair) );         //顶点正确与否   
              // std::cout <<  vector_true[i][m] <<std::endl;
            }

            l_loop = l_loop + num_vector[m];
          }

        l_loop = l_loop /2 ;                   //对称矩阵只考虑上半区的值

        if (l_loop > l_odometry_ave*upper_threshold_constraint)   //upper threshold
        {
          L_vector[id1][id2]=1;
          std::cout << "l_loop:  " <<l_loop  <<"  l_odometry_ave*0.5: "<<l_odometry_ave*upper_threshold_constraint<< std::endl;
        }
          else  if (l_loop<l_odometry_ave*lower_threshold_constraint)    //lower threshold
        {
          L_vector[id1][id2]=0;
          std::cout << "l_loop:  " <<l_loop <<"  l_odometry_ave*0.1: "<<l_odometry_ave*lower_threshold_constraint<< std::endl;
        }
        else          //     l_loop:  l_odometry_ave*0.1------------l_odometry_ave*0.5         L_vector[id1][id2](l_odometry_ave*0.1) = 0.028  ;   L_vector[id1][id2](l_odometry_ave*(0.1+0.5)/2) = 0.41
        {
          L_vector[id1][id2]= exp( ( -(l_loop - l_odometry_ave*upper_threshold_constraint)*(l_loop - l_odometry_ave*upper_threshold_constraint) ) /  (2*l_odometry_ave*l_odometry_ave*sigma_constraint*sigma_constraint) );     //
          std::cout << "l_loop:  " <<l_loop<<"  "<<L_vector[id1][id2] << std::endl;
        }
        
        // std:: cout << " " << std::endl;
    }
  }
  
    // second E step  filter false point pairs in correct constraints
  void Expectation_point(int iteration_num)       //再次使用EM算法，筛除正确loop 和 odometry中的错误点对
  {            
    sum_L_P_=0. ;
    for (int i = 0; i < NumOdometryConstraints(); i++) 
    {
      // std::cout << "num of correct odometry:  " << i<< std::endl;

      FramedMatches match = odometry_txt_.data_[i];
      
      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;
      // std::cout << id1 << "   " << id2 <<std::endl;
        if(1)         ///////////////////////////////////////////////////////////
        {
            // odometry cases
          for (std::size_t j = 0; j < point_num_for_optimize_second; j++) 
          {
              if(vector_true[i+NumLoopClosureConstraints()][j]==0)  continue;

              Eigen::Vector3d vec_d = camera_poses_.data_[id1].transformation_se3_ * match.pairs_[j].first 
                                    -camera_poses_.data_[id2].transformation_se3_ * match.pairs_[j].second;

              L_P_[i+NumLoopClosureConstraints()][j]= U / (U + exp(2 * log (1. + vec_d.squaredNorm() / (SIGMA_two_EM * SIGMA_two_EM))));          
              
              //if(iteration_num==0)   L_P_[i+NumLoopClosureConstraints()][j]=1;

              sum_L_P_ +=L_P_[i+NumLoopClosureConstraints()][j] ;
          }
        }

    }

        // loop cases
    for (int i = 0; i < NumLoopClosureConstraints(); i ++) 
    {

      FramedMatches match = loop_txt_.data_[i];
      // FramedTransformation trans = loop_log_.data_[i];
      // FramedInformation info = loop_info_.data_[i];

      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;
       

      if(L_vector[id1][id2]==0)  continue;       //根据向量 去除错误loop
      if(L_[id1][id2] < TRUST_ODOMETRY*0.2 )  continue;           //只考虑正确loop中错误点对的筛除

      for (std::size_t j = 0; j < point_num_for_optimize_second; j++) 
      {
        if(vector_true[i][j]==0)  continue;

        Eigen::Vector3d vec_d = camera_poses_.data_[id1].transformation_se3_ * match.pairs_[j].first 
                              -camera_poses_.data_[id2].transformation_se3_ * match.pairs_[j].second;

        L_P_[i][j]= U / (U + exp(2 * log (1. + vec_d.squaredNorm() / (SIGMA_two_EM * SIGMA_two_EM))));          
        // if(iteration_num==0)   L_P_[i][j]=1;
        // std::cout << "U:  " << U<< std::endl;
        sum_L_P_ +=L_P_[i][j] ;
        // std::cout << "num of correct loop:  " << i<< std::endl;
        // std::cout << "correct probability of point "<<j <<" is: "<< L_P_[i][j]<< std::endl;
        // std::cout << "point first is :" << match.pairs_[j].first << "point second is :" << match.pairs_[j].second <<std::endl;
        // if(id2 ==37 && j<30)
        //       {
        //           std::cout << id1 << "   " << id2 <<std::endl;
        //         std::cout << "correct probability of point "<<j <<" is: "<< L_P_[i][j]<< "  ";
        //         std::cout << "point first is :" << match.pairs_[j].first << "point second is :" << match.pairs_[j].second <<std::endl;
        //         std::cout << "vec_d.squaredNorm() is: "<<vec_d.squaredNorm() <<"  U is : "<<U <<std::endl; 
        //       }
      }                                                                                        
      
    }
  }

// second M step filter false point pairs in correct constraints
void Maximization_point() {
    last_lambda_second_ = lambda_second_;
    last_camera_poses_.index_ = camera_poses_.index_;
    last_camera_poses_.data_.clear();
    for (int i = 0; i < NumPoses(); i++) {
      last_camera_poses_.data_.push_back( 
        FramedTransformation (camera_poses_.data_[i].id1_, 
                              camera_poses_.data_[i].id2_, 
                              camera_poses_.data_[i].frame_,
                              camera_poses_.data_[i].transformation_se3_));
    }
    camera_poses_.index_ ++;
    // last_covarance_ = covarance_;

    // Maximize lambda
    lambda_second_ = sum_L_P_ / 50;

    // Maximize pose
    // Build the problem.
    ceres::Problem problem;

    // Specify local update rule for our parameter
    for (std::vector< FramedTransformation >::iterator it = camera_poses_.data_.begin(); 
         it != camera_poses_.data_.end(); it++ ) {
      problem.AddParameterBlock(it->transformation_se3_.data(), Sophus::SE3d::num_parameters,
                                new Sophus::test::LocalParameterizationSE3);
    }

    // Create and add cost functions. Derivatives will be evaluated via
    // automatic differentiation                                //YUJIE暂时不对odometry进行优化
    
        //odometry cases
    for (int i = 0; i < NumOdometryConstraints(); i++) 
    {

      FramedMatches match = odometry_txt_.data_[i];
      
      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;
        if(1)         ///////////////////////////////////////////////////////////
        {

          for (std::size_t j = 0; j < point_num_for_optimize_second; j++) 
          {
            
            if(L_P_[i+NumLoopClosureConstraints()][j]<0.8) continue;
            else L_P_[i+NumLoopClosureConstraints()][j]=1;

            if(match.semantic_label[j]==1 && vector_true[i+NumLoopClosureConstraints()][j] != 0)          //odometry没有进行语义筛除
            {
            ceres::CostFunction* cost_odometry =
                new ceres::AutoDiffCostFunction<CauchyFunctor, 3,
                                              Sophus::SE3d::num_parameters,
                                              Sophus::SE3d::num_parameters>(
                    new CauchyFunctor(match.pairs_[j].first, match.pairs_[j].second, 
                                      id1, id2));
            problem.AddResidualBlock(cost_odometry, 
                                  new ceres::ScaledLoss(new ceres::CauchyLoss(SIGMA_two_EM), vector_true[i+NumLoopClosureConstraints()][j]*L_P_[i+NumLoopClosureConstraints()][j], ceres::TAKE_OWNERSHIP),
                                  camera_poses_.data_[id1].transformation_se3_.data(), 
                                  camera_poses_.data_[id2].transformation_se3_.data());                         
            }
          }
        }

    }

        // loop cases
    for (int i = 0; i < NumLoopClosureConstraints(); i++) 
    {
        
      // std::cout << "Maximization + loops " << (i+1) << " / " << NumLoopClosureConstraints() <<  std::endl;
      // FramedTransformation trans = loop_log_.data_[i];
      // FramedInformation info = loop_info_.data_[i];
      FramedMatches match = loop_txt_.data_[i];
      
      assert(match.id1_ < match.id2_);
      int id1 = match.id1_;
      int id2 = match.id2_;

       if(L_[id1][id2] < TRUST_ODOMETRY*0.2)  continue;          //不进行outlier loop的优化
       if(L_vector[id1][id2]==0) continue;    //向量判断   错误loop直接不参与优化

        for (std::size_t j = 0; j < point_num_for_optimize_second; j++) 
        {
          if(L_[id1][id2] >= TRUST_ODOMETRY*0.2)
          {
              if(L_P_[i][j]<0.8) continue;
               else L_P_[i][j]=1;
          }
          if(match.semantic_label[j] == 1 && vector_true[i][j] != 0)   //semantic information and spatial consistency
          {
          // {
          ceres::CostFunction* cost_loop =
              new ceres::AutoDiffCostFunction<CauchyFunctor, 3,
                                              Sophus::SE3d::num_parameters,
                                              Sophus::SE3d::num_parameters>(
                  new CauchyFunctor(match.pairs_[j].first, match.pairs_[j].second, 
                                    id1, id2));

            problem.AddResidualBlock(cost_loop, 
                                  new ceres::ScaledLoss(new ceres::CauchyLoss(SIGMA_two_EM), vector_true[i][j]*L_P_[i][j], ceres::TAKE_OWNERSHIP),
                                  camera_poses_.data_[id1].transformation_se3_.data(), 
                                  camera_poses_.data_[id2].transformation_se3_.data());
                            
          }

        }
    }

    // Set solver options (precision / method)
    ceres::Solver::Options options;
    // options.max_num_iterations = 1000;
    options.gradient_tolerance = 0.01 * Sophus::Constants<double>::epsilon();
    options.function_tolerance = 0.01 * Sophus::Constants<double>::epsilon();
    // options.linear_solver_type = ceres::DENSE_QR;
    options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;
  
    // Solve
    std::cout << "--------------SOLVING----------------SECOND_EM" << std::endl;
    ceres::Solver::Summary summary;
    Solve(options, &problem, &summary);
    std::cout << summary.BriefReport() << std::endl;
    std::cout << "--------------  DONE ----------------SECOND_EM" << std::endl;
  }
  int NumPoses() {
    return camera_poses_.data_.size();
  }

  int NumOdometryConstraints() {
    return odometry_txt_.data_.size();
  }

  int NumLoopClosureConstraints() {
    return loop_txt_.data_.size();
  }

  bool IsConverged_first() {
    // if (last_lambda_ < 0) {
    //   // not started yet
    //    std::cout << "not start"<< lambda_ << std::endl;
    //   return false;
    // }

    std::cout << "new lambda : "<< lambda_ << std::endl;
  if(last_lambda_ !=-1)
  {
    if (fabs(lambda_ - last_lambda_) > 1) {
      std::cout << "lambda was updated" << std::endl;
      return false;
    }
  }


    for (int i = 0; i < NumPoses(); i++)
    {
      // std::cout << "checking converging pose : ";
      Sophus::SE3d last_pose = last_camera_poses_.data_[i].transformation_se3_;
      Sophus::SE3d current_pose = camera_poses_.data_[i].transformation_se3_;

      double const mse = (last_pose.inverse() * current_pose).log().squaredNorm();
      // std::cout<< mse << "mse" <<std::endl;
      // std::cout<< 10. * Sophus::Constants<double>::epsilon() << "10. * Sophus::Constants<double>::epsilon()" <<std::endl;
      bool const converged = mse < 10. * Sophus::Constants<double>::epsilon();

      std::cout << i << "(" << mse << ", " << converged << "," << last_camera_poses_.index_ << "),";
      if (!converged) {
        std::cout << std::endl;
        return false;
      }
    }

    std::cout << std::endl;
    return true;
  }

  bool IsConverged_second() {
    if (last_lambda_second_ < 0) {
    std::cout << "not start"<< std::endl;
      // not started yet
      return false;
    }

    std::cout << "new lambda_second : "<< lambda_second_ << std::endl;
    if(last_lambda_second_ != -1)
    {
      if (fabs(lambda_second_ - last_lambda_second_) > 0.5 ) {
        std::cout << "lambda two was updated" << std::endl;
        return false;
      }
    }


    for (int i = 0; i < NumPoses(); i++)
    {
      // std::cout << "checking converging pose : ";
      Sophus::SE3d last_pose = last_camera_poses_.data_[i].transformation_se3_;
      Sophus::SE3d current_pose = camera_poses_.data_[i].transformation_se3_;

      double const mse = (last_pose.inverse() * current_pose).log().squaredNorm();
      // bool const converged = mse < 1000000. * Sophus::Constants<double>::epsilon();
      bool const converged = mse < 0.1;

      std::cout << i << "(" << mse << ", " << converged << "," << last_camera_poses_.index_ << "),";
      if (!converged) {
        std::cout << "mse > 1000000. * Sophus::Constants<double>::epsilon()"<<std::endl;
        return false;
      }
    }
    return true;
  }
};



bool test(Sophus::SE3d const& T_w_targ, Sophus::SE3d const& T_w_init,
          Sophus::SE3d::Point const& point_a_init,
          Sophus::SE3d::Point const& point_b) {
  static constexpr int kNumPointParameters = 3;

  // Optimisation parameters.
  Sophus::SE3d T_wr = T_w_init;
  Sophus::SE3d::Point point_a = point_a_init;

  // Build the problem.
  ceres::Problem problem;

  // Specify local update rule for our parameter
  problem.AddParameterBlock(T_wr.data(), Sophus::SE3d::num_parameters,
                            new Sophus::test::LocalParameterizationSE3);

  // Create and add cost functions. Derivatives will be evaluated via
  // automatic differentiation
  ceres::CostFunction* cost_function1 =
      new ceres::AutoDiffCostFunction<TestSE3CostFunctor, Sophus::SE3d::DoF,
                                      Sophus::SE3d::num_parameters>(
          new TestSE3CostFunctor(T_w_targ.inverse()));
  problem.AddResidualBlock(cost_function1, NULL, T_wr.data());

  ceres::CostFunction* cost_function2 =
      new ceres::AutoDiffCostFunction<TestPointCostFunctor, kNumPointParameters,
                                      Sophus::SE3d::num_parameters,
                                      kNumPointParameters>(
          new TestPointCostFunctor(T_w_targ.inverse(), point_b));
  problem.AddResidualBlock(cost_function2, NULL, T_wr.data(), point_a.data());

  // Set solver options (precision / method)
  ceres::Solver::Options options;
  options.gradient_tolerance = 0.01 * Sophus::Constants<double>::epsilon();
  options.function_tolerance = 0.01 * Sophus::Constants<double>::epsilon();
  options.linear_solver_type = ceres::DENSE_QR;

  // Solve
  ceres::Solver::Summary summary;
  Solve(options, &problem, &summary);
  std::cout << summary.BriefReport() << std::endl;

  // Difference between target and parameter
  double const mse = (T_w_targ.inverse() * T_wr).log().squaredNorm();
  bool const passed = mse < 10. * Sophus::Constants<double>::epsilon();
  return passed;
}

template <typename Scalar>
bool CreateSE3FromMatrix(Eigen::Matrix<Scalar, 4, 4> mat) {
  auto se3 = Sophus::SE3<Scalar>(mat);
  se3 = se3;
  return true;
}

int main(int argc, char** argv) {
  // using SE3Type = Sophus::SE3<double>;
  // using SO3Type = Sophus::SO3<double>;
  // using Point = SE3Type::Point;
  // double const kPi = Sophus::Constants<double>::pi();

  // std::vector<SE3Type> se3_vec;
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(0.2, 0.5, 0.0)), Point(0, 0, 0)));
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(0.2, 0.5, -1.0)), Point(10, 0, 0)));
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(0., 0., 0.)), Point(0, 100, 5)));
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(0., 0., 0.00001)), Point(0, 0, 0)));
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(0., 0., 0.00001)), Point(0, -0.00000001, 0.0000000001)));
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(0., 0., 0.00001)), Point(0.01, 0, 0)));
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(kPi, 0, 0)), Point(4, -5, 0)));
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(0.2, 0.5, 0.0)), Point(0, 0, 0)) *
  //     SE3Type(SO3Type::exp(Point(kPi, 0, 0)), Point(0, 0, 0)) *
  //     SE3Type(SO3Type::exp(Point(-0.2, -0.5, -0.0)), Point(0, 0, 0)));
  // se3_vec.push_back(
  //     SE3Type(SO3Type::exp(Point(0.3, 0.5, 0.1)), Point(2, 0, -7)) *
  //     SE3Type(SO3Type::exp(Point(kPi, 0, 0)), Point(0, 0, 0)) *
  //     SE3Type(SO3Type::exp(Point(-0.3, -0.5, -0.1)), Point(0, 6, 0)));

  // std::vector<Point> point_vec;
  // point_vec.emplace_back(1.012, 2.73, -1.4);
  // point_vec.emplace_back(9.2, -7.3, -4.4);
  // point_vec.emplace_back(2.5, 0.1, 9.1);
  // point_vec.emplace_back(12.3, 1.9, 3.8);
  // point_vec.emplace_back(-3.21, 3.42, 2.3);
  // point_vec.emplace_back(-8.0, 6.1, -1.1);
  // point_vec.emplace_back(0.0, 2.5, 5.9);
  // point_vec.emplace_back(7.1, 7.8, -14);
  // point_vec.emplace_back(5.8, 9.2, 0.0);

  // for (size_t i = 0; i < se3_vec.size(); ++i) {
  //   const int other_index = (i + 3) % se3_vec.size();
  //   bool const passed = test(se3_vec[i], se3_vec[other_index], point_vec[i],
  //                            point_vec[other_index]);
  //   if (!passed) {
  //     std::cerr << "failed!" << std::endl << std::endl;
  //     exit(-1);
  //   }
  // }

  // Eigen::Matrix<ceres::Jet<double, 28>, 4, 4> mat;
  // mat.setIdentity();
  // std::cout << CreateSE3FromMatrix(mat) << std::endl;

  std::cout << "cauchy_em started" << std::endl;
  clock_t start = clock() ; 

  if (argc == 7)
  {
    // double u = 1.0 / 57;

    // converge  detect for EM first and second initial
    double lambda = 0.99;
    R2EM_CauchyUniform r2em_c(lambda);

    // std::cout << "load" << std::endl;
    // r2em_c.LoadOdometryLog(argv[1]);

    // std::cout << "init" << std::endl;
    // r2em_c.LoadOdometryInfo(argv[2]);

    std::cout << "LoadOdometryTxt" << std::endl;
    r2em_c.LoadOdometryTxt(argv[1]);

    std::cout << "LoadLoopTxt" << std::endl;
    r2em_c.LoadLoopTxt(argv[2]);
    // r2em_c.LoadLoopInfo(argv[4]);
    
    // if (argc > 3) {

    std::cout << "InitCameraPosesWithZhou" << std::endl;
    r2em_c.InitCameraPosesWithZhou(argv[3]);
    // } else {
      // r2em_c.InitCameraPoses();  
    // }

    //初始化
  r2em_c.L_P_.resize(r2em_c.NumLoopClosureConstraints()+r2em_c.NumOdometryConstraints());
    for (int i = 0; i < r2em_c.NumLoopClosureConstraints()+r2em_c.NumOdometryConstraints(); i++) {
      r2em_c.L_P_[i].resize(200);                    //
    }

  r2em_c.L_vector.resize(r2em_c.NumPoses());
    for (int i = 0; i < r2em_c.NumPoses(); i++) {
      r2em_c.L_vector[i].resize(r2em_c.NumPoses());
    }

  r2em_c.vector_true.resize(r2em_c.NumLoopClosureConstraints()+r2em_c.NumOdometryConstraints());
    for (int i = 0; i < r2em_c.NumLoopClosureConstraints()+r2em_c.NumOdometryConstraints(); i++) {
      r2em_c.vector_true[i].resize(200);                    //
    }

    std::cout << "EstimateLAMBDA" << std::endl;
    r2em_c.EstimateLAMBDA();

    //r2em_c.SaveCameraPoses("/home/ziquan/my_ws/init_poses.txt");
    //std::cout << "save to /home/ziquan/my_ws/init_poses.txt" << std::endl;
    // LoadFromFile_semantic_p(argv[4]);
  std::cout << "semantic probability matrix" << std::endl;
    r2em_c.Vector();
    std::cout << "START EM" << std::endl;

    //第一次EM迭代，用于筛除错误的loop   first EM iteration
    for (int i = 0 ; i < NUM_ITERATION_FIRST_EM; i ++) {
      r2em_c.Expectation(i);
      r2em_c.Maximization();

// std::string iteration_pose="/home/tang/undergraduate/RobustPCLReconstruction-cauchy_em/data/cauchy/多机/07-10%正确-各代位姿/pose_first"+std::to_string(i)+".txt";
//   r2em_c.SaveCameraPoses(iteration_pose.c_str());

      std::cout << "check 1 " << std::endl;
      

      std::cout << "check EM converges in iteration " << i << std::endl;
      if (r2em_c.IsConverged_first()) {
        std::cout << "EM converges! " << std::endl;
        break;
      }
    }
    clock_t finish_1 = clock() ; 

    // r2em_c.SaveCameraPoses("/home/tang/undergraduate/RobustPCLReconstruction-cauchy_em/data/cauchy/poses_first_EM.txt");
    //第二次EM迭代，用于筛除正确loop中的错误点对
    for (int i = 0 ; i < NUM_ITERATION_SECOND_EM; i ++) {
      r2em_c.Expectation_point(i); 
      r2em_c.Maximization_point();

// std::string iteration_pose="/home/tang/undergraduate/RobustPCLReconstruction-cauchy_em/data/cauchy/多机/07-10%正确-各代位姿/pose_second"+std::to_string(i)+".txt";
//   r2em_c.SaveCameraPoses(iteration_pose.c_str());

      std::cout << "check 2" << std::endl;

      std::cout << "check EM 2 converges in iteration " << i << std::endl;
      if (r2em_c.IsConverged_second()) {
        std::cout << "EM second converges! " << std::endl;
        break;
      }
    }
    clock_t finish_2 = clock() ; 
    // if (argc > 3) {

      r2em_c.SaveCameraPoses(argv[5]);
      std::cout << "final poses are saved to " << argv[5] << std::endl;
    // } else {

    //   r2em_c.SaveCameraPoses("/home/ziquan/my_ws/final_poses_em.txt");
    //   std::cout << "final poses are saved to /home/ziquan/my_ws/final_poses_em.txt" << std::endl;
    // }


      // r2em_c.SaveLinks(argv[6]);
      std::cout << "links are saved to " << argv[6] << std::endl;
    // r2em_c.SaveLinks("/home/ziquan/my_ws/reg_refine_all_em.log");
    // std::cout << "links are saved to /home/ziquan/my_ws/reg_refine_all_em.log" << std::endl;

    std::cout << "there are " << r2em_c.NumPoses() << " poses" << std::endl;
    // std::cout << "there are " << r2em_c.NumOdometryConstraints() << " odometry constraints" << std::endl;
    std::cout << "there are " << r2em_c.NumLoopClosureConstraints() << " loops" << std::endl;

    std::cout << "first EM time is : " << double((finish_1-start)/1000) << "ms" << std::endl;
    std::cout << "second EM time is : " << double((finish_2-finish_1)/1000) << "ms" << std::endl;
    // std::cout << start << std::endl;
    // std::cout << finish_1 << std::endl;
    // std::cout << finish_2 << std::endl;
    //  std::cout << CLOCKS_PER_SEC << std::endl;
    
  
  
  
  } else {
    std::cout << "input format is : odom.txt loop.txt init.txt final.txt link.txt" << std::endl;
  }
  
  return 0;
}
