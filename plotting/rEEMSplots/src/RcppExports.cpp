// Generated by using Rcpp::compileAttributes() -> do not edit by hand
// Generator token: 10BE3573-1514-4C36-9D1C-5A225CD40393

#include <RcppEigen.h>
#include <Rcpp.h>

using namespace Rcpp;

// tiles2contours_standardize
Rcpp::List tiles2contours_standardize(const Eigen::VectorXd& tiles, const Eigen::VectorXd& rates, const Eigen::MatrixXd& seeds, const Eigen::MatrixXd& marks, const std::string& distm);
RcppExport SEXP rEEMSplots_tiles2contours_standardize(SEXP tilesSEXP, SEXP ratesSEXP, SEXP seedsSEXP, SEXP marksSEXP, SEXP distmSEXP) {
BEGIN_RCPP
    Rcpp::RObject rcpp_result_gen;
    Rcpp::RNGScope rcpp_rngScope_gen;
    Rcpp::traits::input_parameter< const Eigen::VectorXd& >::type tiles(tilesSEXP);
    Rcpp::traits::input_parameter< const Eigen::VectorXd& >::type rates(ratesSEXP);
    Rcpp::traits::input_parameter< const Eigen::MatrixXd& >::type seeds(seedsSEXP);
    Rcpp::traits::input_parameter< const Eigen::MatrixXd& >::type marks(marksSEXP);
    Rcpp::traits::input_parameter< const std::string& >::type distm(distmSEXP);
    rcpp_result_gen = Rcpp::wrap(tiles2contours_standardize(tiles, rates, seeds, marks, distm));
    return rcpp_result_gen;
END_RCPP
}
// tiles2contours
Rcpp::List tiles2contours(const Eigen::VectorXd& tiles, const Eigen::VectorXd& rates, const Eigen::MatrixXd& seeds, const Eigen::MatrixXd& marks, const std::string& distm);
RcppExport SEXP rEEMSplots_tiles2contours(SEXP tilesSEXP, SEXP ratesSEXP, SEXP seedsSEXP, SEXP marksSEXP, SEXP distmSEXP) {
BEGIN_RCPP
    Rcpp::RObject rcpp_result_gen;
    Rcpp::RNGScope rcpp_rngScope_gen;
    Rcpp::traits::input_parameter< const Eigen::VectorXd& >::type tiles(tilesSEXP);
    Rcpp::traits::input_parameter< const Eigen::VectorXd& >::type rates(ratesSEXP);
    Rcpp::traits::input_parameter< const Eigen::MatrixXd& >::type seeds(seedsSEXP);
    Rcpp::traits::input_parameter< const Eigen::MatrixXd& >::type marks(marksSEXP);
    Rcpp::traits::input_parameter< const std::string& >::type distm(distmSEXP);
    rcpp_result_gen = Rcpp::wrap(tiles2contours(tiles, rates, seeds, marks, distm));
    return rcpp_result_gen;
END_RCPP
}
