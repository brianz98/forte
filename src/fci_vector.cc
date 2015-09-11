#include <cmath>

#include "libmints/vector.h"
#include "libmints/matrix.h"

#include "helpers.h"
#include "fci_vector.h"

namespace psi{ namespace forte{

SharedMatrix FCIWfn::C1;
SharedMatrix FCIWfn::Y1;
size_t   FCIWfn::sizeC1 = 0;
//FCIWfn* FCIWfn::tmp_wfn1 = nullptr;
//FCIWfn* FCIWfn::tmp_wfn2 = nullptr;

double FCIWfn::hdiag_timer = 0.0;
double FCIWfn::h1_aa_timer = 0.0;
double FCIWfn::h1_bb_timer = 0.0;
double FCIWfn::h2_aaaa_timer = 0.0;
double FCIWfn::h2_aabb_timer = 0.0;
double FCIWfn::h2_bbbb_timer = 0.0;

FCIIntegrals::FCIIntegrals(std::shared_ptr<StringLists> lists, std::shared_ptr<ForteIntegrals>  ints)
{
    nmo_ = lists->ncmo();
    nmo2_ = nmo_ * nmo_;
    nmo3_ = nmo_ * nmo_ * nmo_;
    nmo4_ = nmo_ * nmo_ * nmo_ * nmo_;

    // This is a warning, but please do not delete it.  It is used in fci_vector.  
    IntegralType integral_type_ = ints->integral_type();

    std::vector<size_t> cmo_to_mo = lists->cmo_to_mo();

    std::vector<size_t> fomo_to_mo = lists->fomo_to_mo();
    size_t nfomo = fomo_to_mo.size();

    oei_a_.resize(nmo2_);
    oei_b_.resize(nmo2_);
    tei_aa_.resize(nmo4_);
    tei_ab_.resize(nmo4_);
    tei_bb_.resize(nmo4_);
    diag_tei_aa_.resize(nmo2_);
    diag_tei_ab_.resize(nmo2_);
    diag_tei_bb_.resize(nmo2_);
    frozen_core_energy_ = ints->frozen_core_energy();

    // Compute the scalar contribution to the energy that comes from
    // the restricted occupied orbitals
    scalar_energy_ = ints->scalar();
    for (size_t i = 0; i < nfomo; ++i){
        size_t ii = fomo_to_mo[i];
        scalar_energy_ += ints->oei_a(ii,ii);
        scalar_energy_ += ints->oei_b(ii,ii);
        for (size_t j = 0; j < nfomo; ++j){
            size_t jj = fomo_to_mo[j];
            scalar_energy_ += 0.5 * ints->aptei_aa(ii,jj,ii,jj);
            scalar_energy_ += 1.0 * ints->aptei_ab(ii,jj,ii,jj);
            scalar_energy_ += 0.5 * ints->aptei_bb(ii,jj,ii,jj);
        }
    }

    for (size_t p = 0; p < nmo_; ++p){
        size_t pp = cmo_to_mo[p];
        for (size_t q = 0; q < nmo_; ++q){
            size_t qq = cmo_to_mo[q];
            size_t pq = nmo_ * p + q;
            oei_a_[pq] = ints->oei_a(pp,qq);
            oei_b_[pq] = ints->oei_b(pp,qq);
            diag_tei_aa_[pq] = ints->diag_aptei_aa(pp,qq);//,pp,qq);
            diag_tei_ab_[pq] = ints->diag_aptei_ab(pp,qq);//,pp,qq);
            diag_tei_bb_[pq] = ints->diag_aptei_bb(pp,qq);//,pp,qq);

            // Compute the one-body contribution to the energy that comes from
            // the restricted occupied orbitals
            for (size_t f = 0; f < nfomo; ++f){
                size_t ff = fomo_to_mo[f];
                oei_a_[pq] += ints->aptei_aa(pp,ff,qq,ff);
                oei_a_[pq] += ints->aptei_ab(pp,ff,qq,ff);
                oei_b_[pq] += ints->aptei_bb(pp,ff,qq,ff);
                oei_b_[pq] += ints->aptei_ab(ff,pp,ff,qq); // TODO check these factors 0.5
            }
            for (size_t r = 0; r < nmo_; ++r){
                size_t rr = cmo_to_mo[r];
                for (size_t s = 0; s < nmo_; ++s){
                    size_t ss = cmo_to_mo[s];
                    size_t pqrs = tei_index(p,q,r,s);
                    tei_aa_[pqrs] = ints->aptei_aa(pp,qq,rr,ss);
                    tei_ab_[pqrs] = ints->aptei_ab(pp,qq,rr,ss);
                    tei_bb_[pqrs] = ints->aptei_bb(pp,qq,rr,ss);
                }
            }
        }
    }
}

FCIIntegrals::FCIIntegrals(std::shared_ptr<ForteIntegrals> ints, std::shared_ptr<MOSpaceInfo> mospace_info,FCIIntegralsType type)
{
    std::vector<size_t> cmo_to_mo;
    std::vector<size_t> fomo_to_mo;

    if (type == Active){
        nmo_ = mospace_info->size("ACTIVE");
        cmo_to_mo = mospace_info->get_corr_abs_mo("ACTIVE");
        fomo_to_mo = mospace_info->get_corr_abs_mo("RESTRICTED_DOCC");
    }else if (type == Correlated){
        nmo_ = mospace_info->size("CORRELATED");
        cmo_to_mo = mospace_info->get_corr_abs_mo("ACTIVE");
    }

    nmo2_ = nmo_ * nmo_;
    nmo3_ = nmo_ * nmo_ * nmo_;
    nmo4_ = nmo_ * nmo_ * nmo_ * nmo_;

    //size_t nfomo =  mospace_info->size("RESTRICTED_DOCC");
	size_t nfomo = fomo_to_mo.size();

    oei_a_.resize(nmo2_);
    oei_b_.resize(nmo2_);
    tei_aa_.resize(nmo4_);
    tei_ab_.resize(nmo4_);
    tei_bb_.resize(nmo4_);
    diag_tei_aa_.resize(nmo2_);
    diag_tei_ab_.resize(nmo2_);
    diag_tei_bb_.resize(nmo2_);


    frozen_core_energy_ = ints->frozen_core_energy();

    // Compute the scalar contribution to the energy that comes from
    // the restricted occupied orbitals
    scalar_energy_ = ints->scalar();
    for (size_t i = 0; i < nfomo; ++i){
        size_t ii = fomo_to_mo[i];
        scalar_energy_ += ints->oei_a(ii,ii);
        scalar_energy_ += ints->oei_b(ii,ii);
        for (size_t j = 0; j < nfomo; ++j){
            size_t jj = fomo_to_mo[j];
            scalar_energy_ += 0.5 * ints->aptei_aa(ii,jj,ii,jj);
            scalar_energy_ += 1.0 * ints->aptei_ab(ii,jj,ii,jj);
            scalar_energy_ += 0.5 * ints->aptei_bb(ii,jj,ii,jj);
        }
    }

    for (size_t p = 0; p < nmo_; ++p){
        size_t pp = cmo_to_mo[p];
        for (size_t q = 0; q < nmo_; ++q){
            size_t qq = cmo_to_mo[q];
            size_t idx = nmo_ * p + q;
            oei_a_[idx] = ints->oei_a(pp,qq);
            oei_b_[idx] = ints->oei_b(pp,qq);
			diag_tei_aa_[idx] = ints->diag_aptei_aa(pp,qq);//,pp,qq);
            diag_tei_ab_[idx] = ints->diag_aptei_ab(pp,qq);//,pp,qq);
            diag_tei_bb_[idx] = ints->diag_aptei_bb(pp,qq);//,pp,qq);


            // Compute the one-body contribution to the energy that comes from
            // the restricted occupied orbitals
            for (size_t f = 0; f < nfomo; ++f){
                size_t ff = fomo_to_mo[f];
                oei_a_[idx] += ints->aptei_aa(pp,ff,qq,ff);
                oei_a_[idx] += ints->aptei_ab(pp,ff,qq,ff);
                oei_b_[idx] += ints->aptei_bb(pp,ff,qq,ff);
                oei_b_[idx] += ints->aptei_ab(ff,pp,ff,qq); // TODO check these factors 0.5
            }
            for (size_t r = 0; r < nmo_; ++r){
                size_t rr = cmo_to_mo[r];
                for (size_t s = 0; s < nmo_; ++s){
                    size_t ss = cmo_to_mo[s];
                    size_t tei_index = nmo3_ * p + nmo2_ * q + nmo_ * r + s;
                    tei_aa_[tei_index] = ints->aptei_aa(pp,qq,rr,ss);
                    tei_ab_[tei_index] = ints->aptei_ab(pp,qq,rr,ss);
                    tei_bb_[tei_index] = ints->aptei_bb(pp,qq,rr,ss);
                }
            }
        }
    }

}

void FCIWfn::allocate_temp_space(std::shared_ptr<StringLists> lists_, size_t)
{
    // TODO Avoid allocating and deallocating these temp

    size_t nirreps = lists_->nirrep();
    size_t maxC1 = 0;
    for(size_t Ia_sym = 0; Ia_sym < nirreps; ++Ia_sym){
        maxC1 = std::max(maxC1,lists_->alfa_graph()->strpi(Ia_sym));
    }
    for(size_t Ib_sym = 0; Ib_sym < nirreps; ++Ib_sym){
        maxC1 = std::max(maxC1,lists_->beta_graph()->strpi(Ib_sym));
    }

    // Allocate the temporary arrays C1 and Y1 with the largest sizes
    C1 = SharedMatrix(new Matrix("C1",maxC1,maxC1));
    Y1 = SharedMatrix(new Matrix("Y1",maxC1,maxC1));

    outfile->Printf("\n  Allocating memory for the Hamiltonian algorithm. Size: 2 x %zu x %zu.   Memory: %8.6f GB",maxC1,maxC1,to_gb(2 * maxC1 * maxC1));

    sizeC1 = maxC1 * maxC1 * static_cast<size_t>(sizeof(double));

    outfile->Printf("\n  Allocating temporary CI vectors");
//    tmp_wfn1 = new FCIWfn(lists_,fci_ints,symmetry);
//    tmp_wfn2 = new FCIWfn(lists_,fci_ints,symmetry);
}

void FCIWfn::release_temp_space()
{
//    outfile->Printf("\n  Deallocating temporary space and scratch vector");
//    delete tmp_wfn1;
//    delete tmp_wfn2;
}

//void FCIWfn::check_temp_space()
//{
//  if (C1 == nullptr || Y1 == nullptr) {
//    outfile->Printf("\n\n  The temp space required by FCIWfn is not allocated!!!");
//    outfile->Flush();
//    exit(1);
//  }
//}

FCIWfn::FCIWfn(std::shared_ptr<StringLists> lists, size_t symmetry)
    : symmetry_(symmetry),  lists_(lists),
      alfa_graph_(lists_->alfa_graph()), beta_graph_(lists_->beta_graph())
{
    startup();
}

FCIWfn::~FCIWfn()
{
    cleanup();
}

///**
// * Copy data from moinfo and allocate the memory
// */
void FCIWfn::startup()
{
    //  if((not Process::environment.options.get_bool("FASTALGRM")) and (moinfo->get_nfocc() > 0)){
    //    printf("\n\nCapriccio execution terminated");
    //    printf("\nThe Hamiltonian algorithm with FASTALGRM = FALSE does not support frozen core MOs\n");
    //    outfile->Printf("\n\n  Capriccio execution terminated");
    //    outfile->Printf("\n  The Hamiltonian algorithm with FASTALGRM = FALSE does not support frozen core MOs\n");
    //    outfile->Flush();
    //    exit(1);
    //  }

    nirrep_ = lists_->nirrep();
    ncmo_ = lists_->ncmo();
    cmopi_ = lists_->cmopi();
    cmopi_offset_ = lists_->cmopi_offset();
//    cmo_to_mo_ = lists_->cmo_to_mo();

    ndet_ = 0;
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        size_t beta_sym = alfa_sym ^ symmetry_;
        size_t detpi = alfa_graph_->strpi(alfa_sym) * beta_graph_->strpi(beta_sym);
        ndet_ += detpi;
        detpi_.push_back(detpi);
    }

    // Allocate the symmetry blocks of the wave function
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        size_t beta_sym = alfa_sym ^ symmetry_;
        //    outfile->Printf("\n\n  Block %d: allocate %d * %d",alfa_sym,(int)alfa_graph_->strpi(alfa_sym),(int)beta_graph_->strpi(beta_sym));
        C_.push_back(SharedMatrix(new Matrix("C",alfa_graph_->strpi(alfa_sym),beta_graph_->strpi(beta_sym))));
    }
}

/**
 * Dellocate the memory
 */
void FCIWfn::cleanup()
{
}

///**
// * Set the wave function to a single Slater determinant
// */
//void FCIWfn::set_to(Determinant& det)
//{
//  zero();
//  DetAddress add = get_det_address(det);
//  coefficients[add.alfa_sym][add.alfa_string][add.beta_string] = 1.0;
//}

///**
// * Get the coefficient of a single Slater determinant
// */
//double FCIWfn::get_coefficient(Determinant& det)
//{
//  DetAddress add = get_det_address(det);
//  return coefficients[add.alfa_sym][add.alfa_string][add.beta_string];
//}

/**
 * Set the wave function to another wave function
 */
void FCIWfn::copy(FCIWfn& wfn)
{
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        C_[alfa_sym]->copy(wfn.C_[alfa_sym]);
    }
}

void FCIWfn::copy(SharedVector vec)
{
    size_t I = 0;
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        int beta_sym = alfa_sym ^ symmetry_;
        size_t maxIa = alfa_graph_->strpi(alfa_sym);
        size_t maxIb = beta_graph_->strpi(beta_sym);
        double** C_ha = C_[alfa_sym]->pointer();
        for(size_t Ia = 0; Ia < maxIa; ++Ia){
            for(size_t Ib = 0; Ib < maxIb; ++Ib){
                C_ha[Ia][Ib] = vec->get(I);
                I += 1;
            }
        }
    }
}

void FCIWfn::copy_to(SharedVector vec)
{
    size_t I = 0;
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        int beta_sym = alfa_sym ^ symmetry_;
        size_t maxIa = alfa_graph_->strpi(alfa_sym);
        size_t maxIb = beta_graph_->strpi(beta_sym);
        double** C_ha = C_[alfa_sym]->pointer();
        for(size_t Ia = 0; Ia < maxIa; ++Ia){
            for(size_t Ib = 0; Ib < maxIb; ++Ib){
                vec->set(I,C_ha[Ia][Ib]);
                I += 1;
            }
        }
    }
}

void FCIWfn::set(std::vector<std::tuple<size_t,size_t,size_t,double>>& sparse_vec)
{
    zero();
    double C;
    size_t h,Ia,Ib;
    for (auto& el : sparse_vec){
        std::tie(h,Ia,Ib,C) = el;
        C_[h]->set(Ia,Ib,C);
    }
}

///**
// * Set the wave function to the nth determinant in the list
// */
//void FCIWfn::set_to(int n)
//{
//  int k = 0;
//  for(int h = 0; h < nirrep_; ++h){
//    int beta_sym = h ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(h);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        if(k == n){
//          coefficients[h][Ia][Ib] = 1.0;
//        }else{
//          coefficients[h][Ia][Ib] = 0.0;
//        }
//        k++;
//      }
//    }
//  }
//}

///**
// * Get the coefficient of the nth determinant in the list
// */
//double FCIWfn::get(int n)
//{
//  int k = 0;
//  double c = 0.0;
//  for(int h = 0; h < nirrep_; ++h){
//    int beta_sym = h ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(h);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        if(k == n){
//          c = coefficients[h][Ia][Ib];
//        }
//        k++;
//      }
//    }
//  }
//  return c;
//}

///**
// * Get a vector of the determinants with weight greather than alpha
// */
//vector<int> FCIWfn::get_important(double alpha)
//{
//  int k = 0;
//  vector<int> list;
//  for(int h = 0; h < nirrep_; ++h){
//    int beta_sym = h ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(h);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        if(std::fabs(coefficients[h][Ia][Ib]) >= alpha){
//          list.push_back(k);
//        }
//        k++;
//      }
//    }
//  }
//  return list;
//}



/////**
//// * Get a vector of the determinants with weight greather than alpha
//// */
////vector<int> FCIWfn::get_sorted_important()
////{
////  vector<pair<double,int> > list;
////  for(int h = 0; h < nirrep_; ++h){
////    int beta_sym = h ^ symmetry_;
////    size_t maxIa = alfa_graph_->strpi(h);
////    size_t maxIb = beta_graph_->strpi(beta_sym);
////    for(size_t Ia = 0; Ia < maxIa; ++Ia){
////      for(size_t Ib = 0; Ib < maxIb; ++Ib){
////        list.push_back(make_pair(std::fabs(coefficients[h][Ia][Ib]),k));
////        k++;
////      }
////    }
////  }
////  sort(list.begin(),list.end(),std::greater<pair<double,int> >());
////  return list;
////}


/**
 * Normalize the wave function without changing the phase
 */
void FCIWfn::normalize()
{
    double factor = norm(2.0);
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        int beta_sym = alfa_sym ^ symmetry_;
        size_t maxIa = alfa_graph_->strpi(alfa_sym);
        size_t maxIb = beta_graph_->strpi(beta_sym);
        double** C_ha = C_[alfa_sym]->pointer();
        for(size_t Ia = 0; Ia < maxIa; ++Ia){
            for(size_t Ib = 0; Ib < maxIb; ++Ib){
                C_ha[Ia][Ib] /= factor;
            }
        }
    }
}

///**
// * Normalize the wave function wrt to a single Slater determinant
// */
//void FCIWfn::randomize()
//{
//  for(int h = 0; h < nirrep_; ++h){
//    int beta_sym = h ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(h);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        coefficients[h][Ia][Ib] += 0.001 * static_cast<double>(std::rand()) / static_cast<double>(RAND_MAX);
//      }
//    }
//  }
//}

/**
 * Zero the wave function
 */
void FCIWfn::zero()
{
    for(SharedMatrix C_h : C_) { C_h->zero(); }
}

///**
// * Zero a symmetry block of the wave function
// * @param h symmetry of the alpha strings of the block to zero
// */
//void FCIWfn::zero_block(int h)
//{
//  int beta_sym = h ^ symmetry_;
//  size_t size = alfa_graph_->strpi(h) * beta_graph_->strpi(beta_sym) * static_cast<size_t> (sizeof(double));
//  if(size > 0)
//    memset(&(coefficients[h][0][0]), 0, size);
//}

///**
// * Transpose a block of the matrix (Works only for total symmetric wfns!)
// * @param h symmetry of the alpha strings of the block to zero
// */
//void FCIWfn::transpose_block(int h)
//{
//  int beta_sym = h ^ symmetry_;
//  size_t maxIa = alfa_graph_->strpi(h);
//  size_t maxIb = beta_graph_->strpi(beta_sym);
//  size_t size = maxIa * maxIb * static_cast<size_t> (sizeof(double));
//  if(size > 0){
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = Ia + 1; Ib < maxIb; ++Ib){
//        double temp = coefficients[h][Ia][Ib];
//        coefficients[h][Ia][Ib] = coefficients[h][Ib][Ia];
//        coefficients[h][Ib][Ia] = temp;
//      }
//    }
//  }
//}

/**
 * Compute the 2-norm of the wave function
 */
double FCIWfn::norm(double power)
{
    double norm = 0.0;
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        int beta_sym = alfa_sym ^ symmetry_;
        size_t maxIa = alfa_graph_->strpi(alfa_sym);
        size_t maxIb = beta_graph_->strpi(beta_sym);
        double** C_ha = C_[alfa_sym]->pointer();
        for(size_t Ia = 0; Ia < maxIa; ++Ia){
            for(size_t Ib = 0; Ib < maxIb; ++Ib){
                norm += std::pow(std::fabs(C_ha[Ia][Ib]),power);
            }
        }
    }
    return std::pow(norm,1.0/power);
}


/**
 * Compute the dot product with another wave function
 */
double FCIWfn::dot(FCIWfn& wfn)
{
    double dot = 0.0;
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        dot += C_[alfa_sym]->vector_dot(wfn.C_[alfa_sym]);
    }
    return(dot);

//        int beta_sym = alfa_sym ^ symmetry_;
//        size_t maxIa = alfa_graph_->strpi(alfa_sym);
//        size_t maxIb = beta_graph_->strpi(beta_sym);

//        double** Ca = C_[alfa_sym]->pointer();
//        double** Cb = wfn.C_[alfa_sym]->pointer();
//        for(size_t Ia = 0; Ia < maxIa; ++Ia){
//            for(size_t Ib = 0; Ib < maxIb; ++Ib){
//                dot += coefficients[alfa_sym][Ia][Ib] * wfn.coefficients[alfa_sym][Ia][Ib];
//            }
//        }
}

///**
// * Find the largest element in the wave function
// */
//double FCIWfn::max_element()
//{
//  double maxelement = 0.0;
//  for(int alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
//    int beta_sym = alfa_sym ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(alfa_sym);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        if (std::abs(coefficients[alfa_sym][Ia][Ib]) > std::abs(maxelement)){
//          maxelement = coefficients[alfa_sym][Ia][Ib];
//        }
//      }
//    }
//  }
//  return maxelement;
//}

///**
// * Find the smallest element in the wave function
// */
//double FCIWfn::min_element()
//{
//  double min_element = 0.0;
//  for(int alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
//    int beta_sym = alfa_sym ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(alfa_sym);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        min_element = std::min(min_element,coefficients[alfa_sym][Ia][Ib]);
//      }
//    }
//  }
//  return min_element;
//}


///**
// * Implements the update method of Bendazzoli and Evangelisti modified
// */
//void FCIWfn::two_update(double alpha,double E,FCIWfn& H,FCIWfn& R)
//{
//  for(int alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
//    int beta_sym = alfa_sym ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(alfa_sym);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        double r = R.coefficients[alfa_sym][Ia][Ib];
//        double h = H.coefficients[alfa_sym][Ia][Ib];
//        double c =   coefficients[alfa_sym][Ia][Ib];
//        coefficients[alfa_sym][Ia][Ib] = - r / (h - E + alpha * c * c - 2.0 * r * c);
//      }
//    }
//  }
//}

///**
// * Implements the update method of Bendazzoli and Evangelisti
// */
//void FCIWfn::bendazzoli_update(double alpha,double E,FCIWfn& H,FCIWfn& R)
//{
//  for(int alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
//    int beta_sym = alfa_sym ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(alfa_sym);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        double r = R.coefficients[alfa_sym][Ia][Ib];
//        double h = H.coefficients[alfa_sym][Ia][Ib];
//        double c =   coefficients[alfa_sym][Ia][Ib];
//        coefficients[alfa_sym][Ia][Ib] -= r / (h - E + alpha * c * c - 2.0 * r * c);
//      }
//    }
//  }
//}

///**
// * Implements the update method of Davidson and Liu
// */
//void FCIWfn::davidson_update(double E,FCIWfn& H,FCIWfn& R)
//{
//  for(int alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
//    int beta_sym = alfa_sym ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(alfa_sym);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        double r = R.coefficients[alfa_sym][Ia][Ib];
//        double h = H.coefficients[alfa_sym][Ia][Ib];
//        if(std::fabs(h-E) > 1.0e-8){
//          coefficients[alfa_sym][Ia][Ib] = - r / (h - E);
//        }else {
//          coefficients[alfa_sym][Ia][Ib] = 0.0;
//          outfile->Printf("\n  WARNING: skipped determinant in davidson update. h - E = %20.12f",h-E);
//        }
//      }
//    }
//  }
//}

///**
// * Add a scaled amount of another wave function
// */
//void FCIWfn::plus_equal(double factor,FCIWfn& wfn)
//{
//  for(int alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
//    int beta_sym = alfa_sym ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(alfa_sym);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        coefficients[alfa_sym][Ia][Ib] += factor * wfn.coefficients[alfa_sym][Ia][Ib];
//      }
//    }
//  }
//}

///**
// * Add a scaled amount of another wave function
// */
//void FCIWfn::scale(double factor)
//{
//  for(int alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
//    int beta_sym = alfa_sym ^ symmetry_;
//    size_t maxIa = alfa_graph_->strpi(alfa_sym);
//    size_t maxIb = beta_graph_->strpi(beta_sym);
//    for(size_t Ia = 0; Ia < maxIa; ++Ia){
//      for(size_t Ib = 0; Ib < maxIb; ++Ib){
//        coefficients[alfa_sym][Ia][Ib] *= factor;
//      }
//    }
//  }
//}

/**
 * Print the non-zero contributions to the wave function
 */
void FCIWfn::print()
{
    // print the non-zero elements of the wave function
    size_t det = 0;
    for(size_t alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        int beta_sym = alfa_sym ^ symmetry_;
        double** C_ha = C_[alfa_sym]->pointer();
        for(size_t Ia = 0; Ia < alfa_graph_->strpi(alfa_sym); ++Ia){
            for(size_t Ib = 0; Ib < beta_graph_->strpi(beta_sym); ++Ib){
                if(std::fabs(C_ha[Ia][Ib]) > 1.0e-9){
                    outfile->Printf("\n  %15.9f [%1d][%2d][%2d] (%d)",
                                    C_ha[Ia][Ib],
                                    alfa_sym, static_cast<int>(Ia), static_cast<int>(Ib),
                                    static_cast<int>(det));
                }
                ++det;
            }
        }
    }
}




///**
// * Apply the same-spin two-particle Hamiltonian to the wave function
// * @param alfa flag for alfa or beta component, true = alfa, false = beta
// */
//void FCIWfn::H2_aaaa2(FCIWfn& result, bool alfa)
//{
//  for(int alfa_sym = 0; alfa_sym < nirreps; ++alfa_sym){
//    int beta_sym = alfa_sym ^ symmetry_;
//    if(detpi[alfa_sym] > 0){
//      double** Y = alfa ? result.coefficients[alfa_sym] : Y1;
//      double** C = alfa ? coefficients[alfa_sym]        : C1;
//      if(!alfa){
//        memset(&(C[0][0]), 0, sizeC1);
//        memset(&(Y[0][0]), 0, sizeC1);
//        size_t maxIa = alfa_graph_->strpi(alfa_sym);
//        size_t maxIb = beta_graph_->strpi(beta_sym);
//        // Copy C transposed in C1
//        for(size_t Ia = 0; Ia < maxIa; ++Ia)
//          for(size_t Ib = 0; Ib < maxIb; ++Ib)
//            C[Ib][Ia] = coefficients[alfa_sym][Ia][Ib];
//      }

//      size_t maxL = alfa ? beta_graph_->strpi(beta_sym) : alfa_graph_->strpi(alfa_sym);
//      // Loop over (p>q) == (p>q)
//      for(int pq_sym = 0; pq_sym < nirreps; ++pq_sym){
//        size_t max_pq = lists->get_pairpi(pq_sym);
//        for(size_t pq = 0; pq < max_pq; ++pq){
//          const Pair& pq_pair = lists->get_nn_list_pair(pq_sym,pq);
//          int p_abs = pq_pair.first;
//          int q_abs = pq_pair.second;
//          double integral = alfa ? tei_aaaa(p_abs,p_abs,q_abs,q_abs)
//                                 : tei_bbbb(p_abs,p_abs,q_abs,q_abs); // Grab the integral
//          integral -= alfa ? tei_aaaa(p_abs,q_abs,q_abs,p_abs)
//                           : tei_bbbb(p_abs,q_abs,q_abs,p_abs); // Grab the integral

//          std::vector<StringSubstitution>& OO = alfa ? lists->get_alfa_oo_list(pq_sym,pq,alfa_sym)
//                                                     : lists->get_beta_oo_list(pq_sym,pq,beta_sym);

//          size_t maxss = OO.size();
//          for(size_t ss = 0; ss < maxss; ++ss)
//            C_DAXPY(maxL,static_cast<double>(OO[ss].sign) * integral, &C[OO[ss].I][0], 1, &Y[OO[ss].J][0], 1);
//        }
//      }
//      // Loop over (p>q) > (r>s)
//      for(int pq_sym = 0; pq_sym < nirreps; ++pq_sym){
//        size_t max_pq = lists->get_pairpi(pq_sym);
//        for(size_t pq = 0; pq < max_pq; ++pq){
//          const Pair& pq_pair = lists->get_nn_list_pair(pq_sym,pq);
//          int p_abs = pq_pair.first;
//          int q_abs = pq_pair.second;
//          for(size_t rs = 0; rs < pq; ++rs){
//              const Pair& rs_pair = lists->get_nn_list_pair(pq_sym,rs);
//              int r_abs = rs_pair.first;
//              int s_abs = rs_pair.second;
//              double integral = alfa ? tei_aaaa(p_abs,r_abs,q_abs,s_abs)
//                                     : tei_bbbb(p_abs,r_abs,q_abs,s_abs); // Grab the integral
//              integral -= alfa ? tei_aaaa(p_abs,s_abs,q_abs,r_abs)
//                               : tei_bbbb(p_abs,s_abs,q_abs,r_abs); // Grab the integral

//              {
//                std::vector<StringSubstitution>& VVOO = alfa ? lists->get_alfa_vvoo_list(p_abs,q_abs,r_abs,s_abs,alfa_sym)
//                                                             : lists->get_beta_vvoo_list(p_abs,q_abs,r_abs,s_abs,beta_sym);
//                // TODO loop in a differen way
//                size_t maxss = VVOO.size();
//                for(size_t ss = 0; ss < maxss; ++ss)
//                  C_DAXPY(maxL,static_cast<double>(VVOO[ss].sign) * integral, &C[VVOO[ss].I][0], 1, &Y[VVOO[ss].J][0], 1);
//              }
//              {
//                std::vector<StringSubstitution>& VVOO = alfa ? lists->get_alfa_vvoo_list(r_abs,s_abs,p_abs,q_abs,alfa_sym)
//                                                             : lists->get_beta_vvoo_list(r_abs,s_abs,p_abs,q_abs,beta_sym);
//                // TODO loop in a differen way
//                size_t maxss = VVOO.size();
//                for(size_t ss = 0; ss < maxss; ++ss)
//                  C_DAXPY(maxL,static_cast<double>(VVOO[ss].sign) * integral, &C[VVOO[ss].I][0], 1, &Y[VVOO[ss].J][0], 1);
//              }
//          }
//        }
//      }
//      if(!alfa){
//        size_t maxIa = alfa_graph_->strpi(alfa_sym);
//        size_t maxIb = beta_graph_->strpi(beta_sym);
//        // Add Y1 transposed to Y
//        for(size_t Ia = 0; Ia < maxIa; ++Ia)
//          for(size_t Ib = 0; Ib < maxIb; ++Ib)
//            result.coefficients[alfa_sym][Ia][Ib] += Y[Ib][Ia];
//      }
//    }
//  } // End loop over h
//}

}}