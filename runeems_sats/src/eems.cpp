
#include "eems.hpp"

EEMS::EEMS(const Params &params) {
  this->params = params;
  
  ofstream out; string outfile = params.mcmcpath + "/eemsrun.txt";
  out.open(outfile.c_str(), ofstream::out);
  check_condition(out.is_open(), "Cannot open " + outfile + " for writing");
  out << "Input parameter values:" << endl << params << endl;
  out.close();
  
  draw.initialize(params.seed);
  habitat.generate_outer(params.datapath, params.mcmcpath);
  graph.generate_grid(params.datapath,params.gridpath, params.mcmcpath,
		      habitat,params.nDemes,params.nIndiv);
  o = graph.get_num_obsrv_demes();
  d = graph.get_num_total_demes();
  n = params.nIndiv;
  p = params.nSites;
  initialize_diffs();
  if (params.diploid) {
    Bconst = 1.0;  Wconst = 2.0;
  } else {
    Bconst = 0.25; Wconst = 1.0;
  }
}
EEMS::~EEMS( ) { }
///////////////////////////////////////////
string EEMS::datapath( ) const { return params.datapath; }
string EEMS::mcmcpath( ) const { return params.mcmcpath; }
string EEMS::prevpath( ) const { return params.prevpath; }
string EEMS::gridpath( ) const { return params.gridpath; }
// Draw points randomly inside the habitat: the habitat is two-dimensional, so
// a point is represented as a row in a matrix with two columns
void EEMS::randpoint_in_habitat(MatrixXd &Seeds) {
  for (int i = 0 ; i < Seeds.rows() ; i++ ) {
    bool in = false;
    double x,y;
    while (!in) {
      x = habitat.get_xmin() + habitat.get_xspan() * draw.runif();
      y = habitat.get_ymin() + habitat.get_yspan() * draw.runif();
      in = habitat.in_point(x,y);
    }
    Seeds(i,0) = x;
    Seeds(i,1) = y;
  }
}
void EEMS::rnorm_effects(const double HalfInterval, const double rateS2, VectorXd &Effcts) {
  for (int i = 0 ; i < Effcts.rows() ; i++ ) {
    Effcts(i) = draw.rtrnorm(0.0,rateS2,HalfInterval);
  }
}
void EEMS::initialize_diffs( ) {
  cout << "[Diffs::initialize]" << endl;
  int alleles_to_read = 0;
  if (params.diploid) {
    alleles_to_read = 2*p;
  } else {
    alleles_to_read = p;
  }
  MatrixXd Sites = readMatrixXd(params.datapath + ".sites");
  // Use `stringstream` instead of `to_string` to convert int to string.
  stringstream alleles_to_read_str; alleles_to_read_str << alleles_to_read;
  check_condition(Sites.rows() == n && Sites.cols() == alleles_to_read,
		  "Check that the genotype matrix is a nIndiv-by-" + alleles_to_read_str.str() + " matrix.");
  cout << "  Read genotype data from " << params.datapath + ".diffs" << endl;
  ///////////////////////////////////////
  J = MatrixXd::Zero(n,o);
  for ( int i = 0 ; i < n ; i ++ ) {
    J(i,graph.get_deme_of_indiv(i)) = 1;
  }
  MatrixXd Diffs_allSites = MatrixXd::Zero(n,n);
  MatrixXd Pairs_allSites = MatrixXd::Zero(n,n);
  ///////////////////////////////////////
  logn.resize(p);
  nmin1.resize(p);
  ll_atfixdf = 0.0;
  for ( int i = 0 ; i < p ; i ++ ) {
    VectorXd z = VectorXd::Zero(n);
    if (params.diploid) {
      VectorXd a1 = Sites.col(2*i);
      VectorXd a2 = Sites.col(2*i+1);
      check_condition(((a1.array() < 0) != (a2.array() < 0)).maxCoeff() == 0,
		      "One allele is missing but the other is not.");
      z = 0.5 * a1 + 0.5 * a2;
    } else {
      z = Sites.col(i);
    }
    int ni = (z.array()>=0).count();
    int nimin1 = ni-1;
    // Copy into a vector with the unobserved samples removed.
    int j = 0;
    VectorXd zi = VectorXd::Zero(ni);
    VectorXi oi = VectorXi::Zero(ni);
    for( int j2 = 0 ; j2 < ni ; j2++ ) {
      while (z(j)<0) { j++; }
      zi(j2) = z(j);
      oi(j2) = j++;
    }
    // Indicates whether the sample's allele is observed or missing
    VectorXd I = (z.array()<0).select(VectorXd::Zero(n),VectorXd::Ones(n));
    MatrixXd Pairs = I * I.transpose();
    MatrixXd Ji = slice(J,oi,VectorXi::LinSpaced(o,0,o-1));
    MatrixXd Li = MatrixXd::Constant(nimin1,ni,-1.0);
    Li.topRightCorner(nimin1,nimin1).setIdentity();
    Z.push_back(zi);
    O.push_back(oi);
    L.push_back(Li);
    // Similarity matrix (with rank 1)
    MatrixXd Si = zi*zi.transpose();
    // Dissimilarity matrix
    MatrixXd Di = - 2.0 * Si;
    Di += Si.diagonal().replicate(1,ni);
    Di += Si.diagonal().transpose().replicate(ni,1);
    ll_atfixdf += logdet(Li*Li.transpose()) + nimin1 * (ln_2 + ln_pi) + nimin1 * pseudologdet( - Li*Di*Li.transpose(),1);
    logn(i) = log(ni);
    nmin1(i) = nimin1;
    VectorXd ci = Ji.colwise().sum();
    int o0 = (ci.array()>0).count();
    VectorXi si = VectorXi::Zero(o0);
    j = 0;
    for (int j2 = 0 ; j2 < o0 ; j2 ++ ) {
      while (ci(j)==0) { j++; }
      si(j2) = j++;
    }
    ci = slice(ci,si);
    cvec.push_back(ci);
    cinv.push_back(pow(ci.array(), - 1.0));
    cmin1.push_back(ci.array() - 1.0);
    Diffs.push_back(Di);
    MatrixXd JtDJ = Ji.transpose()*Di*Ji;
    JtDobsJ.push_back(slice(JtDJ,si,si));
    ovec.push_back(si);
    /////////////////////////////////////
    double mu = zi.mean();
    zi.array() -=  mu;
    double sd = sqrt( zi.squaredNorm() / (ni - 1.0) );
    z.array() -= mu;
    z.array() /= sd;
    Si = z*z.transpose();
    // Dissimilarity matrix
    Di = - 2.0 * Si;
    Di += Si.diagonal().replicate(1,n);
    Di += Si.diagonal().transpose().replicate(n,1);
    // Set rows/cols that correspond to missing samples to 0s
    Di.array() *= Pairs.array();
    Diffs_allSites += Di;
    Pairs_allSites += Pairs;
  }
  // It is possible that a pair of individuals i and j have no loci in common,
  // i.e., either i's allele or j's allele is missing at every locus in the data.
  // In that case Diffs_allSites(i, j) = 0, Pairs_allSites(i, j) = 0
  // and Diffs_allSites(i, j) / Pairs_allSites(i, j) = NaN.
  // Let's make the 0s in the denominator 1, to avoid NaN's.
  Pairs_allSites = (Pairs_allSites.array() > 0).select(Pairs_allSites.array(), 1);
  Diffs_allSites = Diffs_allSites.cwiseQuotient(Pairs_allSites);
  JtDobsJ_allSites = J.transpose() * Diffs_allSites * J;
  JtDhatJ_allSites = MatrixXd::Zero(o, o);
  c_allSites = J.colwise().sum();
  cout << "[Diffs::initialize] Done." << endl << endl;
}
void EEMS::initialize_state( ) {
  cout << "[EEMS::initialize_state]" << endl;
  nowsigma2 = VectorXd::Zero(p);
  for (int i = 0 ; i < p ; i++ ) {
    nowsigma2(i) = draw.rinvgam(3.0,1.0);
  }
  // Initialize the two Voronoi tessellations
  nowqtiles = draw.rnegbin(2*o,0.5); // o is the number of observed demes
  nowmtiles = draw.rnegbin(2*o,0.5);
  cout << "  EEMS starts with " << nowqtiles << " qtiles and " << nowmtiles << " mtiles" << endl;
  // Draw the Voronoi centers Coord uniformly within the habitat
  nowqSeeds = MatrixXd::Zero(nowqtiles,2); randpoint_in_habitat(nowqSeeds);
  nowmSeeds = MatrixXd::Zero(nowmtiles,2); randpoint_in_habitat(nowmSeeds);
  nowmrateS2 = draw.rinvgam(0.5,0.5);
  nowqrateS2 = draw.rinvgam(0.5,0.5);
  // Assign migration rates to the Voronoi tiles
  nowmrateMu = params.mrateMuHalfInterval*(2.0*draw.runif() - 1.0);
  // Assign rates to the Voronoi tiles
  nowqEffcts = VectorXd::Zero(nowqtiles); rnorm_effects(params.qEffctHalfInterval,nowqrateS2,nowqEffcts);
  nowmEffcts = VectorXd::Zero(nowmtiles); rnorm_effects(params.mEffctHalfInterval,nowmrateS2,nowmEffcts);
  // Initialize the mapping of demes to qVoronoi tiles, i.e.,
  // For every deme in the graph -- which migration tile does the deme fall into?
  // The "color" of the deme is the index of the tile
  graph.index_closest_to_deme(nowmSeeds,nowmColors);
  // Initialize the mapping of demes to mVoronoi tiles
  graph.index_closest_to_deme(nowqSeeds,nowqColors);
  cout << "[EEMS::initialize_state] Done." << endl << endl;
}
void EEMS::load_final_state( ) {
  cout << "[EEMS::load_final_state]" << endl;  
  MatrixXd tempi; bool error = false;
  tempi = readMatrixXd(params.prevpath + "/lastqtiles.txt");
  if ((tempi.rows()!=1) || (tempi.cols()!=1)) { error = true; }
  nowqtiles = tempi(0,0);
  tempi = readMatrixXd(params.prevpath + "/lastmtiles.txt");
  if ((tempi.rows()!=1) || (tempi.cols()!=1)) { error = true; }
  nowmtiles = tempi(0,0);
  cout << "  EEMS starts with " << nowqtiles << " qtiles and " << nowmtiles << " mtiles" << endl;
  tempi = readMatrixXd(params.prevpath + "/lastthetas.txt");
  if ((tempi.rows()!=p) || (tempi.cols()!=1)) { error = true; }
  nowsigma2 = tempi.col(0);
  tempi = readMatrixXd(params.prevpath + "/lastqhyper.txt");
  if ((tempi.rows()!=1) || (tempi.cols()!=1)) { error = true; }
  nowqrateS2 = tempi(0,0);
  tempi = readMatrixXd(params.prevpath + "/lastmhyper.txt");
  if ((tempi.rows()!=1) || (tempi.cols()!=2)) { error = true; }
  nowmrateMu = tempi(0,0);
  nowmrateS2 = tempi(0,1);
  tempi = readMatrixXd(params.prevpath + "/lastqeffct.txt");
  if ((tempi.rows()!=nowqtiles) || (tempi.cols()!=1)) { error = true; }
  nowqEffcts = tempi.col(0);
  tempi = readMatrixXd(params.prevpath + "/lastmeffct.txt");
  if ((tempi.rows()!=nowmtiles) || (tempi.cols()!=1)) { error = true; }
  nowmEffcts = tempi.col(0);
  nowqSeeds = readMatrixXd(params.prevpath + "/lastqseeds.txt");
  if ((nowqSeeds.rows()!=nowqtiles) || (nowqSeeds.cols()!=2)) { error = true; }
  nowmSeeds = readMatrixXd(params.prevpath + "/lastmseeds.txt");
  if ((nowmSeeds.rows()!=nowmtiles) || (nowmSeeds.cols()!=2)) { error = true; }
  // Initialize the mapping of demes to qVoronoi tiles
  graph.index_closest_to_deme(nowmSeeds,nowmColors);
  // Initialize the mapping of demes to mVoronoi tiles
  graph.index_closest_to_deme(nowqSeeds,nowqColors);
  cout << "[EEMS::load_final_state] Done." << endl << endl;
}
bool EEMS::start_eems(const MCMC &mcmc) {
  bool error = false;
  // The deviation of move proposals is scaled by the habitat range
  params.mSeedsProposalS2x = params.mSeedsProposalS2 * habitat.get_xspan();
  params.mSeedsProposalS2y = params.mSeedsProposalS2 * habitat.get_yspan();
  params.qSeedsProposalS2x = params.qSeedsProposalS2 * habitat.get_xspan();
  params.qSeedsProposalS2y = params.qSeedsProposalS2 * habitat.get_yspan();
  // MCMC draws are stored in memory, rather than saved to disk,
  // so it is important to thin
  int niters = mcmc.num_iters_to_save();
  mcmcmhyper = MatrixXd::Zero(niters,2);
  mcmcqhyper = MatrixXd::Zero(niters,2);
  mcmcthetas = MatrixXd::Zero(niters,p);
  mcmcpilogl = MatrixXd::Zero(niters,2);
  mcmcmtiles = VectorXd::Zero(niters);
  mcmcqtiles = VectorXd::Zero(niters);
  mcmcmRates.clear();
  mcmcqRates.clear();
  mcmcxCoord.clear();
  mcmcyCoord.clear();
  mcmcwCoord.clear();
  mcmczCoord.clear();
  eval_prior();
  eval_likelihood();
  cout << "Input parameters: " << endl << params << endl
       << fixed << setprecision(2)
       << "Initial log prior: " << nowpi << endl
       << "Initial log llike: " << nowll << endl << endl;
  return (is_finite(nowpi) && is_finite(nowll));
}  
MoveType EEMS::choose_move_type( ) {
  double u1 = draw.runif( );
  double u2 = draw.runif( );
  // There are 4 types of proposals:
  // * birth/death (with equal probability)
  // * move a tile (chosen uniformly at random)
  // * update the rate of a tile (chosen uniformly at random)
  // * update the mean migration rate or the degrees of freedom (with equal probability)
  MoveType move = UNKNOWN_MOVE_TYPE;
  if (u1 < 0.25) {
    // Propose birth/death to update the Voronoi tessellation of the effective diversity,
    // with probability params.qVoronoiPr (which is 0.05 by default). Otherwise,
    // propose birth/death to update the Voronoi tessellation of the effective migration.
    if (u2 < params.qVoronoiPr) {
      move = Q_VORONOI_BIRTH_DEATH;
    } else {
      move = M_VORONOI_BIRTH_DEATH;
    }
  } else if (u1 < 0.5) {
    if (u2 < params.qVoronoiPr) {
      move = Q_VORONOI_POINT_MOVE;
    } else {
      move = M_VORONOI_POINT_MOVE;
    }
  } else if (u1 < 0.75) {
    if (u2 < params.qVoronoiPr) {
      move = Q_VORONOI_RATE_UPDATE;
    } else {
      move = M_VORONOI_RATE_UPDATE;
    }
  } else {
    move = M_MEAN_RATE_UPDATE;
  }
  return(move);
}
double EEMS::eval_prior( ) {
  // The parameters should always be in range
  bool inrange = true;
  for ( int i = 0 ; i < nowqtiles ; i++ ) {
    if (!habitat.in_point(nowqSeeds(i,0),nowqSeeds(i,1))) { inrange = false; }
  }
  for ( int i = 0 ; i < nowmtiles ; i++ ) {
    if (!habitat.in_point(nowmSeeds(i,0),nowmSeeds(i,1))) { inrange = false; }
  }
  if (nowqEffcts.cwiseAbs().minCoeff()>params.qEffctHalfInterval) { inrange = false; }
  if (nowmEffcts.cwiseAbs().minCoeff()>params.mEffctHalfInterval) { inrange = false; }
  if (abs(nowmrateMu)>params.mrateMuHalfInterval) { inrange = false; }
  if (!inrange) { return (-Inf); }
  // Use the normal pdf with mean mu and standard deviation sigma to compute
  // normalizing constant for a truncated normal pdf with mean mu, standard
  // deviation sigma and support [lower bound, upper bound].
  boost::math::normal nowmrateNorm(0.0,sqrt(nowmrateS2));
  boost::math::normal nowqrateNorm(0.0,sqrt(nowqrateS2));
  nowpi = 
    + lgamma(params.negBiSize+nowmtiles) - lgamma(nowmtiles+1.0) + nowmtiles*log(params.negBiProb)
    + lgamma(params.negBiSize+nowqtiles) - lgamma(nowqtiles+1.0) + nowqtiles*log(params.negBiProb)
    - (params.mrateShape_2+1.0)*log(nowmrateS2) - params.mrateScale_2/nowmrateS2
    - (params.qrateShape_2+1.0)*log(nowqrateS2) - params.qrateScale_2/nowqrateS2
    - (nowmtiles/2.0)*log(nowmrateS2) - nowmEffcts.squaredNorm()/(2.0*nowmrateS2)
    - (nowqtiles/2.0)*log(nowqrateS2) - nowqEffcts.squaredNorm()/(2.0*nowqrateS2)
    - nowmtiles * log(cdf(nowmrateNorm,params.mEffctHalfInterval) - cdf(nowmrateNorm,-params.mEffctHalfInterval))
    - nowqtiles * log(cdf(nowqrateNorm,params.qEffctHalfInterval) - cdf(nowqrateNorm,-params.qEffctHalfInterval))
    - (params.sigmaShape_2+1.0)*nowsigma2.array().log().sum()
    - params.sigmaScale_2*pow(nowsigma2.array(),-1.0).sum();
  return (nowpi);
}
double EEMS::eval_likelihood( ) {
  // Expected genetic dissimilarities Delta are modeled as
  // Delta(a,b) = BetweenDistance(a,b) + ( WithinDiversity(a) + WithinDiversity(b) )/2
  //            = B(a,b) + ( W(a) + W(b) )/ 2
  // For every deme in the graph -- what is its effective diversity q(a)?
  calc_within(nowqColors,nowqEffcts,nowW);
  // For every pair of demes -- what is the effective resistance distance B(a,b)?
  // Binv is the inverse of B
  calc_between(nowmColors,nowmEffcts,nowmrateMu,nowB);
  // Compute the Wishart log likelihood
  nowll = EEMS_wishpdfln(nowB,nowW,nowsigma2,nowtriDeltaQD);
  return (nowll);
}
void EEMS::calc_within(const VectorXi &qColors, const VectorXd &qEffcts, VectorXd &W) const {
  // o is the number of observed demes in the graph
  if (W.size()!=o) { W.resize(o); }
  // For every observed deme in the graph
  for ( int alpha = 0 ; alpha < o ; alpha++ ) {
    // WithinDiversity = W(a) = 10^( q_alpha ) = 10^( q_tile(tile_alpha))
    W(alpha) = pow(10.0,qEffcts(qColors(alpha)));
  }
  W *= Wconst;
}
void EEMS::calc_between(const VectorXi &mColors, const VectorXd &mEffcts, const double mrateMu, MatrixXd &B) const {
  // d is the number of demes in the graph (observed or not)
  if (B.rows()!=d||B.cols()!=d) { B.resize(d,d); }
  // A sparse matrix of migration rates will be constructed on the fly
  // First a triplet (a,b,m) is added for each edge (a,b)
  // I have decided to construct the sparse matrix rather than update it
  // because a single change to the migration Voronoi tessellation can
  // change the migration rate of many edges simultaneously
  vector<Tri> coefficients;
  int alpha, beta;
  // For every edge in the graph -- it does not matter whether demes are observed or not
  // as the resistance distance takes into consideration all paths between a and b
  // to produces the effective resistance B(a,b)
  for ( int edge = 0 ; edge < graph.get_num_edges() ; edge++ ) {
    graph.get_edge(edge,alpha,beta);
    // On the log10 scale, log10(m_alpha) = mrateMu + m_alpha = mrateMu + m_tile(tile_alpha)
    // On the log10 scale, log10(m_beta) = mrateMu + m_beta = mrateMu + m_tile(tile_beta)
    double log10m_alpha = mrateMu + mEffcts(mColors(alpha));
    double log10m_beta = mrateMu + mEffcts(mColors(beta));
    // Then on the original scale, m(alpha,beta) = (10^m_alpha + 10^m_beta)/2
    double m_ab = 0.5 * pow(10.0,log10m_alpha) + 0.5 * pow(10.0,log10m_beta);
    // The graph is undirected, so m(alpha->beta) = m(beta->alpha)
    coefficients.push_back(Tri(alpha,beta,m_ab));
    coefficients.push_back(Tri(beta,alpha,m_ab));
  }
  SpMat sparseM(d,d);
  // Actually construct and fill in the sparse matrix
  sparseM.setFromTriplets(coefficients.begin(),coefficients.end());
  // Initialize a dense matrix from the sparse matrix
  MatrixXd M = MatrixXd(sparseM);
  // Read S1.4 Computing the resistance distances in the Supplementary
  // This computation is specific to the resistance distance metric
  // but the point is that we have a method to compute the between
  // demes component of the expected genetic dissimilarities from
  // the sparse matrix of migration rates
  // Here instead of B, we compute its inverse Binv
  MatrixXd Hinv = - M; Hinv.diagonal() += M.rowwise().sum(); Hinv.array() += 1.0;
  MatrixXd Binv;
  if (o==d) {
    Binv = -0.5 * Hinv;
  } else {
    Binv = -0.5 * Hinv.topLeftCorner(o,o);
    Binv += 0.5 * Hinv.topRightCorner(o,d-o) *
      Hinv.bottomRightCorner(d-o,d-o).selfadjointView<Lower>().llt().solve(Hinv.bottomLeftCorner(d-o,o));
  }
  // The constant is slightly different for haploid and diploid species 
  B = Bconst * Binv.inverse();
}
/*
  This function implements the computations described in the Section S1.3 in the Supplementary Information,
  "Computing the Wishart log likelihood l(k, m, q, sigma2)", and I have tried to used similar notation
  For example, MatrixXd X = lu.solve(T) is equation (S20)
  since lu is the decomposition of (B*C - W) and T is B * inv(W)
  Returns wishpdfln( -L*D*L' ; - (sigma2/df) * L*Delta(m,q)*L' , df )
 */
double EEMS::EEMS_wishpdfln(const MatrixXd &B, const VectorXd &W, const VectorXd &sigma2, VectorXd &triDeltaQD) const {
  VectorXd ldetDinvQ = VectorXd::Zero(p);
  if (triDeltaQD.size() != p) { triDeltaQD.resize(p); }
  for ( int i = 0 ; i < p ; i++ ) {
    VectorXd Wi = slice(W,ovec[i]);
    VectorXd Wiinv = pow(Wi.array(),-1.0);
    MatrixXd T = slice(B,ovec[i],ovec[i]);
    T *= cvec[i].asDiagonal(); T -= Wi.asDiagonal(); // Now T = B*C - W
    PartialPivLU<MatrixXd> lu(T);
    T.noalias() = T*Wiinv.asDiagonal()*cinv[i].asDiagonal();
    T += cinv[i].asDiagonal();                       // Now T = B*inv(W)
    MatrixXd X = lu.solve(T);
    VectorXd Xc_Winv = X*cvec[i] - Wiinv;
    double oDinvo = cvec[i].dot(Xc_Winv);
    double oDiDDi = Xc_Winv.transpose()*JtDobsJ[i]*Xc_Winv;
    ldetDinvQ(i) = logn(i) - log(abs(oDinvo))
      + cmin1[i].dot(Wiinv.array().log().matrix())
      - lu.matrixLU().diagonal().array().abs().log().sum();
    triDeltaQD(i) = X.cwiseProduct(JtDobsJ[i]).sum() - oDiDDi/oDinvo;
  }
  return ( 0.5 * ( ldetDinvQ.sum() - (triDeltaQD.array() / sigma2.array()).sum() -
		   (nmin1.array() * sigma2.array().log()).sum() - ll_atfixdf ) );
}
double EEMS::eval_proposal_rate_one_qtile(Proposal &proposal) const {
  calc_within(nowqColors,proposal.newqEffcts,proposal.newW);
  return (EEMS_wishpdfln(nowB,proposal.newW,nowsigma2,proposal.newtriDeltaQD));
}
double EEMS::eval_proposal_move_one_qtile(Proposal &proposal) const {
  graph.index_closest_to_deme(proposal.newqSeeds,proposal.newqColors);
  calc_within(proposal.newqColors,nowqEffcts,proposal.newW);
  return (EEMS_wishpdfln(nowB,proposal.newW,nowsigma2,proposal.newtriDeltaQD));
}
double EEMS::eval_birthdeath_qVoronoi(Proposal &proposal) const {
  graph.index_closest_to_deme(proposal.newqSeeds,proposal.newqColors);
  calc_within(proposal.newqColors,proposal.newqEffcts,proposal.newW);
  return (EEMS_wishpdfln(nowB,proposal.newW,nowsigma2,proposal.newtriDeltaQD));
}
double EEMS::eval_proposal_rate_one_mtile(Proposal &proposal) const {
  calc_between(nowmColors,proposal.newmEffcts,nowmrateMu,proposal.newB);
  return (EEMS_wishpdfln(proposal.newB,nowW,nowsigma2,proposal.newtriDeltaQD));
}
double EEMS::eval_proposal_overall_mrate(Proposal &proposal) const {
  calc_between(nowmColors,nowmEffcts,proposal.newmrateMu,proposal.newB);
  return (EEMS_wishpdfln(proposal.newB,nowW,nowsigma2,proposal.newtriDeltaQD));
}
double EEMS::eval_proposal_move_one_mtile(Proposal &proposal) const {
  graph.index_closest_to_deme(proposal.newmSeeds,proposal.newmColors);
  calc_between(proposal.newmColors,nowmEffcts,nowmrateMu,proposal.newB);
  return (EEMS_wishpdfln(proposal.newB,nowW,nowsigma2,proposal.newtriDeltaQD));
}
double EEMS::eval_birthdeath_mVoronoi(Proposal &proposal) const {
  graph.index_closest_to_deme(proposal.newmSeeds,proposal.newmColors);
  calc_between(proposal.newmColors,proposal.newmEffcts,nowmrateMu,proposal.newB);
  return (EEMS_wishpdfln(proposal.newB,nowW,nowsigma2,proposal.newtriDeltaQD));
}
///////////////////////////////////////////
void EEMS::update_sigma2( ) {
  for ( int i = 0 ; i < p ; i++ ) {
    nowpi += (params.sigmaShape_2+1.0)*log(nowsigma2(i)) + params.sigmaScale_2/nowsigma2(i);
    nowll += 0.5 * nowtriDeltaQD(i)/nowsigma2(i) + 0.5 * nmin1(i) * log(nowsigma2(i));
    nowsigma2(i) = draw.rinvgam( params.sigmaShape_2 + 0.5 * nmin1(i),
				 params.sigmaScale_2 + 0.5 * nowtriDeltaQD(i) );
    nowpi -= (params.sigmaShape_2+1.0)*log(nowsigma2(i)) + params.sigmaScale_2/nowsigma2(i);
    nowll -= 0.5 * nowtriDeltaQD(i)/nowsigma2(i) + 0.5 * nmin1(i) * log(nowsigma2(i));
  }
}
void EEMS::propose_rate_one_qtile(Proposal &proposal) {
  // Choose a tile at random to update
  int qtile = draw.runif_int(0,nowqtiles-1);
  // Make a random-walk proposal, i.e., add small offset to current value
  double curqEffct = nowqEffcts(qtile);
  double newqEffct = draw.rnorm(curqEffct,params.qEffctProposalS2);
  proposal.move = Q_VORONOI_RATE_UPDATE;
  proposal.newqEffcts = nowqEffcts;
  proposal.newqEffcts(qtile) = newqEffct;
  // The prior distribution on the tile effects is truncated normal
  // So first check whether the proposed value is in range
  // Then update the prior and evaluate the new likelihood
  //  + (curqEffct*curqEffct) / (2.0*qrateS2) : old prior component associated with this tile
  //  - (newqEffct*newqEffct) / (2.0*qrateS2) : new prior component associated with this tile
  if ( abs(newqEffct) < params.qEffctHalfInterval ) {
    proposal.newpi = nowpi - (newqEffct*newqEffct - curqEffct*curqEffct) / (2.0*nowqrateS2);
    proposal.newll = eval_proposal_rate_one_qtile(proposal);
  } else {
    proposal.newpi = -Inf;
    proposal.newll = -Inf;
  }
}
void EEMS::propose_rate_one_mtile(Proposal &proposal) {
  // Choose a tile at random to update
  int mtile = draw.runif_int(0,nowmtiles-1);
  // Make a random-walk proposal, i.e., add small offset to current value
  double curmEffct = nowmEffcts(mtile);
  double newmEffct = draw.rnorm(curmEffct,params.mEffctProposalS2);
  proposal.move = M_VORONOI_RATE_UPDATE;
  proposal.newmEffcts = nowmEffcts;
  proposal.newmEffcts(mtile) = newmEffct;
  if ( abs(newmEffct) < params.mEffctHalfInterval ) {
    proposal.newpi = nowpi - (newmEffct*newmEffct - curmEffct*curmEffct) / (2.0*nowmrateS2);
    proposal.newll = eval_proposal_rate_one_mtile(proposal);
  } else {
    proposal.newpi = -Inf;
    proposal.newll = -Inf;
  }
}
void EEMS::propose_overall_mrate(Proposal &proposal) {
  // Make a random-walk Metropolis-Hastings proposal
  double newmrateMu = draw.rnorm(nowmrateMu,params.mrateMuProposalS2);
  proposal.move = M_MEAN_RATE_UPDATE;
  proposal.newmrateMu = newmrateMu;
  // If the proposed value is in range, the prior probability does not change
  // as the prior distribution on mrateMu is uniform
  // Otherwise, setting the prior and the likelihood to -Inf forces a rejection
  if ( abs(newmrateMu) < params.mrateMuHalfInterval ) {
    proposal.newpi = nowpi;
    proposal.newll = eval_proposal_overall_mrate(proposal);
  } else {
    proposal.newpi = -Inf;
    proposal.newll = -Inf;
  }
}
void EEMS::propose_move_one_qtile(Proposal &proposal) {
  // Choose a tile at random to move
  int qtile = draw.runif_int(0,nowqtiles-1);
  // Make a random-walk proposal, i.e., add small offset to current value
  // In this case, there are actually two values -- longitude and latitude
  double newqSeedx = draw.rnorm(nowqSeeds(qtile,0),params.qSeedsProposalS2x);
  double newqSeedy = draw.rnorm(nowqSeeds(qtile,1),params.qSeedsProposalS2y);
  proposal.move = Q_VORONOI_POINT_MOVE;
  proposal.newqSeeds = nowqSeeds;
  proposal.newqSeeds(qtile,0) = newqSeedx;
  proposal.newqSeeds(qtile,1) = newqSeedy;
  if (habitat.in_point(newqSeedx,newqSeedy)) {
    proposal.newpi = nowpi;
    proposal.newll = eval_proposal_move_one_qtile(proposal);
  } else {
    proposal.newpi = -Inf;
    proposal.newll = -Inf;
  }
}
void EEMS::propose_move_one_mtile(Proposal &proposal) {
  // Choose a tile at random to move
  int mtile = draw.runif_int(0,nowmtiles-1);
  // Make a random-walk proposal, i.e., add small offset to current value
  // In this case, there are actually two values -- longitude and latitude
  double newmSeedx = draw.rnorm(nowmSeeds(mtile,0),params.mSeedsProposalS2x);
  double newmSeedy = draw.rnorm(nowmSeeds(mtile,1),params.mSeedsProposalS2y);
  proposal.move = M_VORONOI_POINT_MOVE;
  proposal.newmSeeds = nowmSeeds;
  proposal.newmSeeds(mtile,0) = newmSeedx;
  proposal.newmSeeds(mtile,1) = newmSeedy;
  if (habitat.in_point(newmSeedx,newmSeedy)) {
    proposal.newpi = nowpi;
    proposal.newll = eval_proposal_move_one_mtile(proposal);
  } else {
    proposal.newpi = -Inf;
    proposal.newll = -Inf;
  }
}
void EEMS::propose_birthdeath_qVoronoi(Proposal &proposal) {
  int newqtiles = nowqtiles,r;
  double u = draw.runif();
  double pBirth = 0.5;
  double pDeath = 0.5;
  proposal.newqEffcts = nowqEffcts;
  proposal.newqSeeds = nowqSeeds;
  // If there is exactly one tile, rule out a death proposal
  if ((nowqtiles==1) || (u<0.5)) { // Propose birth
    if (nowqtiles==1) { pBirth = 1.0; }
    newqtiles++; // Birth means adding a tile
    MatrixXd newqSeed = MatrixXd::Zero(1,2); randpoint_in_habitat(newqSeed);
    pairwise_distance(nowqSeeds,newqSeed).col(0).minCoeff(&r);
    // The new tile is assigned a rate by perturbing the current rate at the new seed    
    double nowqEffct = nowqEffcts(r);
    double newqEffct = draw.rtrnorm(nowqEffct,params.qEffctProposalS2,params.qEffctHalfInterval);
    insertRow(proposal.newqSeeds,newqSeed.row(0));
    insertElem(proposal.newqEffcts,newqEffct);
    // Compute log(proposal ratio) and log(prior ratio)
    proposal.newratioln = log(pDeath/pBirth)
      - dtrnormln(newqEffct,nowqEffct,params.qEffctProposalS2,params.qEffctHalfInterval);
    proposal.newpi = nowpi
      + log((nowqtiles+params.negBiSize)/(newqtiles/params.negBiProb))
      + dtrnormln(newqEffct,0.0,nowqrateS2,params.qEffctHalfInterval);
  } else {                      // Propose death
    if (nowqtiles==2) { pBirth = 1.0; }
    newqtiles--; // Death means removing a tile
    int qtileToRemove = draw.runif_int(0,newqtiles);
    MatrixXd oldqSeed = nowqSeeds.row(qtileToRemove);
    removeRow(proposal.newqSeeds,qtileToRemove);
    removeElem(proposal.newqEffcts,qtileToRemove);
    pairwise_distance(proposal.newqSeeds,oldqSeed).col(0).minCoeff(&r);
    double nowqEffct = proposal.newqEffcts(r);
    double oldqEffct = nowqEffcts(qtileToRemove);
    // Compute log(prior ratio) and log(proposal ratio)
    proposal.newratioln = log(pBirth/pDeath)
      + dtrnormln(oldqEffct,nowqEffct,params.qEffctProposalS2,params.qEffctHalfInterval);
    proposal.newpi = nowpi
      + log((nowqtiles/params.negBiProb)/(newqtiles+params.negBiSize))
      - dtrnormln(oldqEffct,0.0,nowqrateS2,params.qEffctHalfInterval);
  }
  proposal.move = Q_VORONOI_BIRTH_DEATH;
  proposal.newqtiles = newqtiles;
  proposal.newll = eval_birthdeath_qVoronoi(proposal);
}
void EEMS::propose_birthdeath_mVoronoi(Proposal &proposal) {
  int newmtiles = nowmtiles,r;
  double u = draw.runif();
  double pBirth = 0.5;
  double pDeath = 0.5;
  proposal.newmEffcts = nowmEffcts;
  proposal.newmSeeds = nowmSeeds;
  if ((nowmtiles==1) || (u<0.5)) { // Propose birth
    if (nowmtiles==1) { pBirth = 1.0; }
    newmtiles++; // Birth means adding a tile
    MatrixXd newmSeed = MatrixXd::Zero(1,2); randpoint_in_habitat(newmSeed);
    pairwise_distance(nowmSeeds,newmSeed).col(0).minCoeff(&r);
    double nowmEffct = nowmEffcts(r);
    double newmEffct = draw.rtrnorm(nowmEffct,params.mEffctProposalS2,params.mEffctHalfInterval);
    insertRow(proposal.newmSeeds,newmSeed.row(0));
    insertElem(proposal.newmEffcts,newmEffct);
    // Compute log(prior ratio) and log(proposal ratio)
    proposal.newratioln = log(pDeath/pBirth)
      - dtrnormln(newmEffct,nowmEffct,params.mEffctProposalS2,params.mEffctHalfInterval);
    proposal.newpi = nowpi
      + log((nowmtiles+params.negBiSize)/(newmtiles/params.negBiProb))
      + dtrnormln(newmEffct,0.0,nowmrateS2,params.mEffctHalfInterval);
  } else {                      // Propose death
    if (nowmtiles==2) { pBirth = 1.0; }
    newmtiles--; // Death means removing a tile
    int mtileToRemove = draw.runif_int(0,newmtiles);
    MatrixXd oldmSeed = nowmSeeds.row(mtileToRemove);
    removeRow(proposal.newmSeeds,mtileToRemove);
    removeElem(proposal.newmEffcts,mtileToRemove);
    pairwise_distance(proposal.newmSeeds,oldmSeed).col(0).minCoeff(&r);
    double nowmEffct = proposal.newmEffcts(r);
    double oldmEffct = nowmEffcts(mtileToRemove);
    // Compute log(prior ratio) and log(proposal ratio)
    proposal.newratioln = log(pBirth/pDeath)
      + dtrnormln(oldmEffct,nowmEffct,params.mEffctProposalS2,params.mEffctHalfInterval);
    proposal.newpi = nowpi
      + log((nowmtiles/params.negBiProb)/(newmtiles+params.negBiSize))
      - dtrnormln(oldmEffct,0.0,nowmrateS2,params.mEffctHalfInterval);
  }
  proposal.move = M_VORONOI_BIRTH_DEATH;
  proposal.newmtiles = newmtiles;
  proposal.newll = eval_birthdeath_mVoronoi(proposal);
}
void EEMS::update_hyperparams( ) {
  double SSq = nowqEffcts.squaredNorm();
  double SSm = nowmEffcts.squaredNorm();
  nowqrateS2 = draw.rinvgam(params.qrateShape_2 + 0.5 * nowqtiles, params.qrateScale_2 + 0.5 * SSq);
  nowmrateS2 = draw.rinvgam(params.mrateShape_2 + 0.5 * nowmtiles, params.mrateScale_2 + 0.5 * SSm);
  nowpi = eval_prior();
}
bool EEMS::accept_proposal(Proposal &proposal) {
  double u = draw.runif( );
  // The proposal cannot be accepted because the prior is 0
  // This can happen if the proposed value falls outside the parameter's support
  if ( proposal.newpi == -Inf ) {
    proposal.newpi = nowpi;
    proposal.newll = nowll;
    return false;
  }
  double ratioln = proposal.newpi - nowpi + proposal.newll - nowll;
  // If the proposal is either birth or death, add the log(proposal ratio)
  if (proposal.move==Q_VORONOI_BIRTH_DEATH || proposal.move==M_VORONOI_BIRTH_DEATH) {
    ratioln += proposal.newratioln;
  }
  if ( log(u) < min(0.0,ratioln) ) {
    switch (proposal.move) {
    case Q_VORONOI_RATE_UPDATE:
      nowqEffcts = proposal.newqEffcts;
      nowW = proposal.newW;
      break;
    case Q_VORONOI_POINT_MOVE:
      nowqSeeds = proposal.newqSeeds;
      nowqColors = proposal.newqColors;
      nowW = proposal.newW;
      break;
    case Q_VORONOI_BIRTH_DEATH:
      nowqSeeds = proposal.newqSeeds;
      nowqEffcts = proposal.newqEffcts;
      nowqtiles = proposal.newqtiles;
      nowqColors = proposal.newqColors;
      nowW = proposal.newW;
      break;
    case M_VORONOI_RATE_UPDATE:
      nowmEffcts = proposal.newmEffcts;
      nowB = proposal.newB;
      break;
    case M_MEAN_RATE_UPDATE:
      nowmrateMu = proposal.newmrateMu;
      nowB = proposal.newB;
      break;
    case M_VORONOI_POINT_MOVE:
      nowmSeeds = proposal.newmSeeds;
      nowmColors = proposal.newmColors;
      nowB = proposal.newB;
      break;
    case M_VORONOI_BIRTH_DEATH:
      nowmSeeds = proposal.newmSeeds;
      nowmEffcts = proposal.newmEffcts;
      nowmtiles = proposal.newmtiles;
      nowmColors = proposal.newmColors;
      nowB = proposal.newB;
      break;
    default:
      cerr << "[RJMCMC] Unknown move type" << endl;
      exit(1);
    }
    nowpi = proposal.newpi;
    nowll = proposal.newll;
    nowtriDeltaQD = proposal.newtriDeltaQD;
    return true;
  } else {
    proposal.newpi = nowpi;
    proposal.newll = nowll;
    return false;
  }
}
///////////////////////////////////////////
void EEMS::print_iteration(const MCMC &mcmc) const {
  cout << " Ending iteration " << mcmc.currIter
       << " with acceptance proportions:" << endl << mcmc
       << "         number of qVoronoi tiles = " << nowqtiles << endl
       << "         number of mVoronoi tiles = " << nowmtiles << endl
       << "          Log prior = " << nowpi << endl
       << "          Log llike = " << nowll << endl;
}
void EEMS::save_iteration(const MCMC &mcmc) {
  int iter = mcmc.index_saved_iteration( );
  mcmcthetas.row(iter) = nowsigma2;
  mcmcqhyper(iter,0) = 0.0;
  mcmcqhyper(iter,1) = nowqrateS2;
  mcmcmhyper(iter,0) = nowmrateMu;
  mcmcmhyper(iter,1) = nowmrateS2;
  mcmcpilogl(iter,0) = nowpi;
  mcmcpilogl(iter,1) = nowll;
  mcmcqtiles(iter) = nowqtiles;
  mcmcmtiles(iter) = nowmtiles;
  for ( int t = 0 ; t < nowqtiles ; t++ ) {
    mcmcqRates.push_back(pow(10.0,nowqEffcts(t)));
  }
  for ( int t = 0 ; t < nowqtiles ; t++ ) {
    mcmcwCoord.push_back(nowqSeeds(t,0));
  }
  for ( int t = 0 ; t < nowqtiles ; t++ ) {
    mcmczCoord.push_back(nowqSeeds(t,1));
  }
  for ( int t = 0 ; t < nowmtiles ; t++ ) {
    mcmcmRates.push_back(pow(10.0,nowmEffcts(t) + nowmrateMu));
  }
  for ( int t = 0 ; t < nowmtiles ; t++ ) {
    mcmcxCoord.push_back(nowmSeeds(t,0));
  }
  for ( int t = 0 ; t < nowmtiles ; t++ ) {
    mcmcyCoord.push_back(nowmSeeds(t,1));
  }
  MatrixXd B = nowB;
  VectorXd h = B.diagonal();    // If B = -2H, then diag(B) = -2diag(H) = -2h
  B -= 0.5 * h.replicate(1,o);  // Therefore 1h' + h1' - 2H = -1diag(B)'/2 - diag(B)1'/2 + B
  B -= 0.5 * h.transpose().replicate(o,1);
  B += 0.5 * nowW.replicate(1,o);
  B += 0.5 * nowW.transpose().replicate(o,1);
  JtDhatJ_allSites += B;   // Instead of JtDhatJ += nowsigma2 * B but this requires standardizing each locus by its sigma2_l.
}
void EEMS::output_current_state( ) const {
  ofstream out;
  out.open((params.mcmcpath + "/lastqtiles.txt").c_str(),ofstream::out);
  check_condition(out.is_open(), "Cannot open " + params.mcmcpath + "/lastqtiles.txt for writing");
  out << nowqtiles << endl;
  out.close( );
  out.open((params.mcmcpath + "/lastmtiles.txt").c_str(),ofstream::out);
  check_condition(out.is_open(), "Cannot open " + params.mcmcpath + "/lastmtiles.txt for writing");
  out << nowmtiles << endl;
  out.close( );
  out.open((params.mcmcpath + "/lastthetas.txt").c_str(),ofstream::out);
  check_condition(out.is_open(), "Cannot open " + params.mcmcpath + "/lasthetas.txt for writing");
  out << fixed << setprecision(6) << nowsigma2 << endl;
  out.close( );
  out.open((params.mcmcpath + "/lastqhyper.txt").c_str(),ofstream::out);
  check_condition(out.is_open(), "Cannot open " + params.mcmcpath + "/lastqhyper.txt for writing");
  out << fixed << setprecision(6) << nowqrateS2 << endl;
  out.close( );
  out.open((params.mcmcpath + "/lastmhyper.txt").c_str(),ofstream::out);
  check_condition(out.is_open(), "Cannot open " + params.mcmcpath + "/lastmhyper.txt for writing");
  out << fixed << setprecision(6) << nowmrateMu << " " << nowmrateS2 << endl;
  out.close( );
  out.open((params.mcmcpath + "/lastpilogl.txt").c_str(),ofstream::out);
  check_condition(out.is_open(), "Cannot open " + params.mcmcpath + "/lastpilogl.txt for writing");
  out << fixed << setprecision(6) << nowpi << " " << nowll << endl;
  out.close( );
  dlmwrite(params.mcmcpath + "/lastmeffct.txt", nowmEffcts);
  dlmwrite(params.mcmcpath + "/lastmseeds.txt", nowmSeeds);
  dlmwrite(params.mcmcpath + "/lastqeffct.txt", nowqEffcts);
  dlmwrite(params.mcmcpath + "/lastqseeds.txt", nowqSeeds);
}
void EEMS::output_results(const MCMC &mcmc) const {
  ofstream out;
  // If there is a single observation in deme alpha,
  // then Pairs = c[alpha] * c[alpha] - c[alpha] = 0
  // and there will be NaN's on the diagonal of the `rdistJtDobsJ` matrix,
  // which is appropriate as we need at least two observations to compute dissimilarities.
  MatrixXd Pairs_allSites = c_allSites*c_allSites.transpose();
  Pairs_allSites -= c_allSites.asDiagonal();
  MatrixXd oDemes = MatrixXd::Zero(o,3);
  int niters = mcmc.num_iters_to_save();
  oDemes << graph.get_the_obsrv_demes(),c_allSites;
  dlmwrite(params.mcmcpath + "/rdistoDemes.txt", oDemes);
  dlmwrite(params.mcmcpath + "/rdistJtDobsJ.txt", JtDobsJ_allSites.cwiseQuotient(Pairs_allSites));
  dlmwrite(params.mcmcpath + "/rdistJtDhatJ.txt", JtDhatJ_allSites/niters);
  dlmwrite(params.mcmcpath + "/mcmcqtiles.txt", mcmcqtiles);
  dlmwrite(params.mcmcpath + "/mcmcmtiles.txt", mcmcmtiles);
  dlmwrite(params.mcmcpath + "/mcmcthetas.txt", mcmcthetas);
  dlmwrite(params.mcmcpath + "/mcmcqhyper.txt", mcmcqhyper);
  dlmwrite(params.mcmcpath + "/mcmcmhyper.txt", mcmcmhyper);
  dlmwrite(params.mcmcpath + "/mcmcpilogl.txt", mcmcpilogl);
  dlmcell(params.mcmcpath + "/mcmcmrates.txt",mcmcmtiles,mcmcmRates);
  dlmcell(params.mcmcpath + "/mcmcxcoord.txt",mcmcmtiles,mcmcxCoord);
  dlmcell(params.mcmcpath + "/mcmcycoord.txt",mcmcmtiles,mcmcyCoord);
  dlmcell(params.mcmcpath + "/mcmcqrates.txt",mcmcqtiles,mcmcqRates);
  dlmcell(params.mcmcpath + "/mcmcwcoord.txt",mcmcqtiles,mcmcwCoord);
  dlmcell(params.mcmcpath + "/mcmczcoord.txt",mcmcqtiles,mcmczCoord);
  output_current_state( );
  out.open((params.mcmcpath + "/eemsrun.txt").c_str(), ofstream::app);
  out << "Acceptance proportions:" << endl << mcmc << endl
      << fixed << setprecision(3)
      << "Final log prior: " << nowpi << endl
      << "Final log llike: " << nowll << endl;
  out.close( );
  cout << fixed << setprecision(3)
       << "Final log prior: " << nowpi << endl
       << "Final log llike: " << nowll << endl;
}
void EEMS::check_ll_computation( ) const {
  double pi0 = test_prior(nowmSeeds,nowmEffcts,nowmrateMu,nowqSeeds,nowqEffcts,nowsigma2,nowmrateS2,nowqrateS2);
  double ll0 = test_likelihood(nowmSeeds,nowmEffcts,nowmrateMu,nowqSeeds,nowqEffcts,nowsigma2);
  check_condition( abs(nowll - ll0) / abs(ll0) < 1e-12, "ll0 != ll");
  check_condition( abs(nowpi - pi0) / abs(pi0) < 1e-12, "pi0 != pi");
}
double EEMS::test_prior(const MatrixXd &mSeeds, const VectorXd &mEffcts, const double mrateMu,
			const MatrixXd &qSeeds, const VectorXd &qEffcts,
			const VectorXd &sigma2, const double mrateS2, const double qrateS2) const {
  bool inrange = true;
  int qtiles = qEffcts.size();
  int mtiles = mEffcts.size();
  // First check that all parameters fall into their support range
  for ( int i = 0 ; i < qtiles ; i++ ) {
    if (!habitat.in_point(qSeeds(i,0),qSeeds(i,1))) { inrange = false; }
  }
  for ( int i = 0 ; i < mtiles ; i++ ) {
    if (!habitat.in_point(mSeeds(i,0),mSeeds(i,1))) { inrange = false; }
  }
  if (qEffcts.cwiseAbs().minCoeff()>params.qEffctHalfInterval) { inrange = false; }
  if (mEffcts.cwiseAbs().minCoeff()>params.mEffctHalfInterval) { inrange = false; }
  if (abs(mrateMu)>params.mrateMuHalfInterval) { inrange = false; }
  if (!inrange) { return (-Inf); }
  // Then compute the prior, on the log scale
  double ln_pi =
    + dnegbinln(mtiles,params.negBiSize,params.negBiProb)
    + dnegbinln(qtiles,params.negBiSize,params.negBiProb)
    + dinvgamln(mrateS2,params.mrateShape_2,params.mrateScale_2)
    + dinvgamln(qrateS2,params.qrateShape_2,params.qrateScale_2);
  for (int i = 0 ; i < p ; i++) {
    ln_pi += dinvgamln(sigma2(i),params.sigmaShape_2,params.sigmaScale_2);
  }
  for (int i = 0 ; i < qtiles ; i++) {
    ln_pi += dtrnormln( qEffcts(i),0.0,qrateS2,params.qEffctHalfInterval);
  }
  for (int i = 0 ; i < mtiles ; i++) {
    ln_pi += dtrnormln( mEffcts(i),0.0,mrateS2,params.mEffctHalfInterval);
  }
  return ln_pi;
}
double EEMS::test_likelihood(const MatrixXd &mSeeds, const VectorXd &mEffcts, const double mrateMu,
			     const MatrixXd &qSeeds, const VectorXd &qEffcts,
			     const VectorXd &sigma2) const {
  // mSeeds, mEffcts and mrateMu define the migration Voronoi tessellation
  // qSeeds, qEffcts define the diversity Voronoi tessellation
  // These are EEMS parameters, so no need to pass them to test_likelihood
  VectorXi mColors, qColors;
  // For every deme in the graph -- which migration tile does the deme fall into? 
  graph.index_closest_to_deme(mSeeds,mColors);
  // For every deme in the graph -- which diversity tile does the deme fall into? 
  graph.index_closest_to_deme(qSeeds,qColors);
  VectorXd W = VectorXd::Zero(d);
  // Transform the log10 diversity parameters into diversity rates on the original scale
  for ( int alpha = 0 ; alpha < d ; alpha++ ) {
    double log10q_alpha = qEffcts(qColors(alpha)); // qrateMu = 0.0
    W(alpha) = pow(10.0,log10q_alpha);
  }
  MatrixXd M = MatrixXd::Zero(d,d);
  int alpha, beta;
  // Transform the log10 migration parameters into migration rates on the original scale
  for ( int edge = 0 ; edge < graph.get_num_edges() ; edge++ ) {
    graph.get_edge(edge,alpha,beta);
    double log10m_alpha = mEffcts(mColors(alpha)) + mrateMu;
    double log10m_beta = mEffcts(mColors(beta)) + mrateMu;
    M(alpha,beta) = 0.5 * pow(10.0,log10m_alpha) + 0.5 * pow(10.0,log10m_beta);
    M(beta,alpha) = M(alpha,beta);
  }
  // J is an indicator matrix such that J(i,a) = 1 if individual i comes from deme a,
  // and J(i,a) = 0 otherwise  
  MatrixXd Delta = expected_dissimilarities(J, M / Bconst, W * Wconst);
  double logl = 0.0;
  for ( int i = 0 ; i < p ; i++ ) {
    // Exactly equation S13
    logl += pseudowishpdfln(-L[i] * Diffs[i] * L[i].transpose(),
			    -L[i] * slice(Delta,O[i],O[i]) * L[i].transpose() * sigma2(i), 1);
  }
  return logl;
}
