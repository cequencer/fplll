/* Copyright (C) 2015-2016 Martin Albrecht, Leo Ducas.

   This file is part of fplll. fplll is free software: you
   can redistribute it and/or modify it under the terms of the GNU Lesser
   General Public License as published by the Free Software Foundation,
   either version 2.1 of the License, or (at your option) any later version.

   fplll is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTAbILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with fplll. If not, see <http://www.gnu.org/licenses/>. */


#include <array>
#include "factorial.const"
#include "ballvol.const"
#include "fplll.h"

FPLLL_BEGIN_NAMESPACE


// naming conventions:
// b is for bound (squared)
// pv is for partial volumes (NOT squared)
// r is for gram schmidt length (squared). Automatically renormalized
// to avoid overflowing partial volumes
// p is for polynomial

// inside this code, b,pv,and R are in reversed order
// as to conform with the algorithm desciption of [ChenThesis]
// reversing output and ouput is done by the C extern function

// n is for the dimension of the basis to prune
// d is for degrees of polynomials. md is for max_degree
// d is floor(n/2 at most). Odd n are dealt with by ignoring the first component



#define PRUNER_MAX_PREC 1000
#define PRUNER_MAX_D 1023
#define PRUNER_MAX_N 2047



template<class FT> 
class Pruner{
  public:
    class TestPruner;
    friend class TestPruner;

    FT preproc_cost;
    FT target_success_proba;
    FT enumeration_radius;

    
    Pruner();

    template<class GSO_ZT,class GSO_FT>
    void load_basis_shape(MatGSO<GSO_ZT, GSO_FT>& gso, int beginning = 0, int end = 0);
    void load_basis_shape(int dim, double* gso_sq_norms);
    
    //void set_parameters(FT preproc_cost, FT target_success_proba);
    
    void optimize_pruning_coeffs(/*io*/double* pr, /*i*/ int reset = 1);
    double get_enum_cost(/*i*/double* pr);
    double get_enum_cost_with_retrials(/*i*/double* pr);
    double get_svp_success_proba(/*i*/double* pr);



  private:
    using vec = array<FT, PRUNER_MAX_N>;
    using evec = array<FT, PRUNER_MAX_D>; 
          // Even vectors, i.e. only one every two entry is stored: V[2i] = V[2i+1] =E[i]
    using poly = array<FT, PRUNER_MAX_D + 1>;


    void set_tabulated_consts();
    int n;  // Dimension of the (sub)-basis
    int d;  // Degree d = floor(n/2)

    vec r;
    vec pv;
    FT renormalization_factor;


    int check_loaded_basis();
    void init_prunning_coeffs(evec &b);
    void load_prunning_coeffs(/*i*/double* pr, /*o*/ evec& b);
    void save_prunning_coeffs(/*o*/double* pr, /*i*/ evec& b);
    inline int enforce(/*io*/ evec &b, /*opt i*/ int j = 0);
    inline FT eval_poly(int ld,/*i*/ poly *p, FT x);
    inline void integrate_poly(int ld,/*io*/ poly *p);
    inline FT relative_volume(/*i*/int rd, evec &b);
    inline FT cost(/*i*/ evec &b);
    inline FT svp_success_proba(/*i*/ evec &b);
    inline FT cost_factor(/*i*/ evec &b);
    FT cost_factor_derivative(/*i*/ evec &b, /*o*/ evec &res);
    int improve(/*io*/ evec &b);
    void descent(/*io*/ evec &b);

    FT tabulated_factorial[PRUNER_MAX_N];
    FT tabulated_ball_vol[PRUNER_MAX_N];
    
    FT one;  // HACK: because we don't have (double - FT) (yet ?)
    FT minus_one; // HACK: same here
  

    FT epsilon;    // Epsilon to use for numerical differentiation
    FT min_step;    // Minimal step in a given direction 
    FT min_cf_decrease;    // Maximal ratio of two consectuive cf in the descent before stopping

    FT step_factor; // Increment factor for steps in a given direction
    FT shell_ratio; // Shell thickness Ratio when evaluating svp proba
    FT symmetry_factor; // Set at 2 for SVP enumeration assuming the implem only explore half the space
};

template<class FT>
Pruner<FT>::Pruner(){
  n = 0;
  d = 0;
  set_tabulated_consts();
  epsilon = std::pow(2., -13);
  min_step = std::pow(2., -12);
  step_factor = std::pow(2, .5);
  shell_ratio = .995;
  minus_one = -1.;
  one = 1.;
  preproc_cost = 0.;
  enumeration_radius = 0.;
  target_success_proba = .90;
  preproc_cost = 0;
  min_cf_decrease = .9999;
  symmetry_factor = 2;
}


template<class FT>
void Pruner<FT>::set_tabulated_consts(){
  mpfr_t tmp;
  mpfr_init2(tmp, PRUNER_MAX_PREC);
  for (int i = 0; i < PRUNER_MAX_N; ++i)
  {
    mpfr_set_str(tmp, pre_factorial[i], 10, MPFR_RNDN);
    tabulated_factorial[i] = tmp;
    mpfr_set_str(tmp, pre_ball_vol[i], 10, MPFR_RNDN);
    tabulated_ball_vol[i] = tmp;
  }
  return;
}


/// Autoprune function, hiding the Pruner class






/// PUBLIC METHODS

template<class FT>
template<class GSO_ZT,class GSO_FT>
void Pruner<FT>::load_basis_shape(MatGSO<GSO_ZT, GSO_FT>& gso, int beginning, int end){
  if (!end){
    end = gso.d;
  }
  n = end - beginning;
  d = n/2;
  if (!d){
    throw std::runtime_error("Inside Pruner : Needs a dimension n>1");
  }
  GSO_FT f;
  FT logvol,tmp;
  logvol = 0;
  for (int i = 0; i < n; ++i)
  {
    gso.getR(f, end - 1 - i, end - 1 - i);
    r[i] = f;
    logvol += log(f);
  }
  tmp = - n;
  renormalization_factor = exp(logvol / tmp);
  for (int i = 0; i < n; ++i)
  {
    r[i] *= renormalization_factor;
  }

  tmp = 1.;
  for (int i = 0; i < 2 * d; ++i) {
    tmp *= sqrt(r[i]);
    pv[i] = tmp;
  }
}

template<class FT>
void Pruner<FT>::load_basis_shape(int dim, double* gso_sq_norms){
  n = dim;
  d = n/2;
  if (!d){
    throw std::runtime_error("Inside Pruner : Needs a dimension n>1");
  }
  FT logvol,tmp;
  logvol = 0;
  for (int i = 0; i < n; ++i)
  {
    r[i] = gso_sq_norms[n - 1 - i];

    logvol += log(r[i]);
  }
  tmp = - n;
  renormalization_factor = exp(logvol / tmp);

  for (int i = 0; i < n; ++i)
  {
    r[i] *= renormalization_factor;

  }
  tmp = 1.;
  for (int i = 0; i < 2 * d; ++i) {
    tmp *= sqrt(r[i]);
    pv[i] = tmp;
  }
}


template<class FT>
double Pruner<FT>::get_svp_success_proba(/*i*/double* pr){
  evec b;
  load_prunning_coeffs(pr, b);
  return svp_success_proba(b).get_d();
}

template<class FT>
double Pruner<FT>::get_enum_cost(/*i*/double* pr){
  evec b;
  load_prunning_coeffs(pr, b);
  return cost(b).get_d();
}

template<class FT>
double Pruner<FT>::get_enum_cost_with_retrials(/*i*/double* pr){
  evec b;
  load_prunning_coeffs(pr, b);
  return cost_factor(b).get_d();
}


template<class FT>
void Pruner<FT>::optimize_pruning_coeffs(/*io*/double* pr, /*i*/ int reset){
  evec b;
  if (reset){
    init_prunning_coeffs(b);
  }
  else{
    load_prunning_coeffs(pr, b);
  }
  descent(b);
  save_prunning_coeffs(pr, b);
}

// PRIVATE METHODS

template<class FT>
void Pruner<FT>::load_prunning_coeffs(/*i*/double* pr, /*o*/ evec& b){
  for (int i = 0; i < d; ++i) {
    b[i] = pr[n - 1 - 2 * i];
  }
  if (enforce(b)){
    throw std::runtime_error(
      "Inside Pruner : Ill formed pruning coefficients (must be decreasing, starting with two 1.0)");
  }
}

template<class FT>
int Pruner<FT>::check_loaded_basis(){
  if (d){
      return 0;
    }
  throw std::runtime_error("Inside Pruner : No basis loaded");
  return 1;
}

template<class FT>
void Pruner<FT>::save_prunning_coeffs(/*o*/double* pr, /*i*/ evec& b){
  for (int i = 0; i < d; ++i) {
    pr[n - 1 - 2 * i] = b[i].get_d();
    pr[n - 2 - 2 * i] = b[i].get_d();
  }
  pr[0] = 1.;
}

template<class FT>
inline int Pruner<FT>::enforce(/*io*/ evec &b, /*opt i*/ int j){
  int status = 0;
  if (b[d - 1] < 1){
    status = 1;
  }
  b[d - 1] = 1;
  for (int i = 0; i < d; ++i){
    if (b[i] > 1) {b[i] = 1; status = 1;}
    if (b[i] <= .1) b[i] = .1;
  }
  for (int i = j; i < d - 1; ++i){
    if (b[i + 1] < b[i]) {b[i + 1] = b[i]; status = 1;}
  }
  for (int i = j - 1; i >= 0; --i){
    if (b[i + 1] < b[i]) {b[i] = b[i + 1]; status = 1;}
  }  
  return status;
}

template<class FT>
inline FT Pruner<FT>::eval_poly(int ld,/*i*/ poly *p, FT x){
  FT acc;
  acc = 0.0;
  for (int i = ld; i >= 0; --i) {
    acc = acc * x;
    acc = acc + (*p)[i];
  }
  return acc;
}

template<class FT>
inline void Pruner<FT>::integrate_poly(int ld,/*io*/ poly *p){
  for (int i = ld; i >= 0; --i) {
    FT tmp;
    tmp = i + 1.;
    (*p)[i + 1] = (*p)[i] / tmp;
  }
  (*p)[0] = 0;
}



template<class FT>
inline FT Pruner<FT>::relative_volume(int rd, /*i*/ evec &b){
  poly P;
  P[0] = 1;
  int ld = 0;
  for (int i = rd - 1; i >= 0; --i) {
    integrate_poly(ld, &P);
    ld++;
    P[0] = minus_one * eval_poly(ld, &P, b[i] / b[rd - 1]);
  }
  if (rd % 2) {
    return minus_one * P[0] * tabulated_factorial[rd];
  } else {
    return P[0] * tabulated_factorial[rd];
  }
}

template<class FT>
inline FT Pruner<FT>::cost(/*i*/ evec &b){
  vec rv; // Relative volumes at each level

  for (int i = 0; i < d; ++i) {

    rv[2 * i + 1] = relative_volume(i + 1, b);
  }

  rv[0] = 1;
  for (int i = 1; i < d; ++i) {
    rv[2 * i] =
        sqrt(rv[2 * i - 1] * rv[2 * i + 1]); // Interpolate even values
  }

  FT total;
  total = 0;
  FT normalized_radius;
  normalized_radius = sqrt(enumeration_radius * renormalization_factor);
  
  for (int i = 0; i < 2 * d; ++i) {
    FT tmp;
    tmp = pow_si(normalized_radius, 1 + i) *
          rv[i] * tabulated_ball_vol[i + 1] *
          sqrt(pow_si(b[i / 2], 1 + i)) / pv[i];











    total += tmp;
  }
  total /= symmetry_factor;  
  //exit(1);
  return total;
}


template<class FT>
inline FT Pruner<FT>::svp_success_proba(/*i*/ evec &b){

  evec b_minus_db;
  FT dx = shell_ratio;

  for (int i = 0; i < d; ++i) {
    b_minus_db[i] = b[i] / (dx * dx);
    if (b_minus_db[i] > 1)
      b_minus_db[i] = 1;
  }

  FT vol = relative_volume(d, b);
  FT dxn = pow_si(dx, 2 * d);
  FT dvol =  dxn * relative_volume(d, b_minus_db) - vol;
  return dvol / (dxn - 1.);

}



template<class FT>
inline FT Pruner<FT>::cost_factor(/*i*/ evec &b){

  FT success_proba = svp_success_proba(b);

  if (success_proba >= target_success_proba)
    return cost(b);

  FT trials =  log(one - target_success_proba) / log(one - success_proba);
  return cost(b) * trials + preproc_cost * (trials-1);
}



template<class FT>
FT Pruner<FT>::cost_factor_derivative(/*i*/ evec &b, /*o*/ evec &res){
  evec bpDb;
  res[d - 1] = 0.;
  for (int i = 0; i < d-1; ++i) {
    bpDb = b;
    bpDb[i] *= (one - epsilon);
    enforce(bpDb, i);
    FT X = cost_factor(bpDb);

    bpDb = b;
    bpDb[i] *= (one + epsilon);
    enforce(bpDb, i);
    FT Y = cost_factor(bpDb);



    res[i] = (log(X) - log(Y)) / epsilon;
  }

}  
template<class FT>
int Pruner<FT>::improve(/*io*/ evec &b){

  FT cf = cost_factor(b);
  FT old_cf = cf;
  evec newb;
  evec gradient;
  cost_factor_derivative(b, gradient);
  FT norm = 0;

  for (int i = 0; i < d; ++i)
  {

  }

  // normalize the gradient
  for (int i = 0; i < d; ++i) {
    norm += gradient[i] * gradient[i];
    newb[i] = b[i];
  }

  norm = sqrt(norm /  (one * (1. * d)) );
  if (norm <= 0.)
    return 0;
  for (int i = 0; i < d; ++i)
  {

  }

  for (int i = 0; i < d; ++i) {
    gradient[i] /= norm;
  }
  FT new_cf;

  FT step = min_step;
  int i;

  for (i = 0;; ++i) {
    for (int i = 0; i < d; ++i) {
      newb[i] = newb[i] + step * gradient[i];
    }

    enforce(newb);
    new_cf = cost_factor(newb);



    if (new_cf >= cf){
      break;
    }
    b = newb;
    cf = new_cf;
    step *= step_factor;
  }

  if (cf > old_cf * min_cf_decrease){
    return 0;
  }
  return i;
}

template<class FT>
void Pruner<FT>::descent(/*io*/ evec &b){
  while (improve(b)) { };
}


template<class FT>
void Pruner<FT>::init_prunning_coeffs(evec &b) {
  for (int i = 0; i < d; ++i) {
    b[i] = .1 + ((1.*i) / d);
  }
  enforce(b);
}







template<class FT, class GSO_ZT, class GSO_FT> 
void auto_prune(/*output*/ double* pr, double& success_proba,
                /*inputs*/ double enumeration_radius, double preproc_cost,
                double target_success_proba,
                MatGSO<GSO_ZT, GSO_FT>& gso, int beginning = 0, int end = 0){

  Pruner<FP_NR<double>> pru;

  pru.enumeration_radius = enumeration_radius;
  pru.target_success_proba = target_success_proba;
  pru.preproc_cost = preproc_cost;
  load_basis_shape(gso,beginning, end);
  pru.optimize_pruning_coeffs(pr);
  success_proba = pru.get_svp_success_proba(pr);
}





FPLLL_END_NAMESPACE







