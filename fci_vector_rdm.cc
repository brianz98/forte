#include <cmath>

#include <boost/timer.hpp>

#include "libmints/molecule.h"

#include <libqt/qt.h>

#include "bitset_determinant.h"
#include "fci_vector.h"

extern int fci_debug_level;

namespace psi{ namespace libadaptive{

/**
 * Compute the one-particle density matrix for a given wave function
 * @param alfa flag for alfa or beta component, true = alfa, false = beta
 */
void FCIWfn::compute_rdms(int max_order)
{
    std::vector<double> rdm_timing;

    if (max_order >= 1){
        boost::timer t;
        compute_1rdm(opdm_a_,true);
        compute_1rdm(opdm_b_,false);
        rdm_timing.push_back(t.elapsed());
    }

    if (max_order >= 2){
        boost::timer t;
        compute_2rdm_aa(tpdm_aa_,true);
        compute_2rdm_aa(tpdm_bb_,false);
        compute_2rdm_ab(tpdm_ab_);
        rdm_timing.push_back(t.elapsed());

    }

    if (max_order >= 3){
        boost::timer t;
        compute_3rdm_aaa(tpdm_aaa_,true);
        compute_3rdm_aaa(tpdm_bbb_,false);
        compute_3rdm_aab(tpdm_aab_,true);
//        compute_3rdm_aab(tpdm_abb_,false);
        rdm_timing.push_back(t.elapsed());
    }

    if (max_order >= 4){
    }

    // Compute the energy from the 1-RDM and 2-RDM
    double nuclear_repulsion_energy = Process::environment.molecule()->nuclear_repulsion_energy();

    double energy_1rdm = 0.0;
    double energy_2rdm = 0.0;

    for (size_t p = 0; p < ncmo_; ++p){
        for (size_t q = 0; q < ncmo_; ++q){
            energy_1rdm += opdm_a_[ncmo_ * p + q] * ints_->oei_a(p,q);
            energy_1rdm += opdm_b_[ncmo_ * p + q] * ints_->oei_b(p,q);
        }
    }

    for (size_t p = 0; p < ncmo_; ++p){
        for (size_t q = 0; q < ncmo_; ++q){
            for (size_t r = 0; r < ncmo_; ++r){
                for (size_t s = 0; s < ncmo_; ++s){
                    energy_2rdm += 0.25 * tpdm_aa_[tei_index(p,q,r,s)] * ints_->aptei_aa(p,q,r,s);
                    energy_2rdm += tpdm_ab_[tei_index(p,q,r,s)] * ints_->aptei_ab(p,q,r,s);
                    energy_2rdm += 0.25 * tpdm_bb_[tei_index(p,q,r,s)] * ints_->aptei_bb(p,q,r,s);
                }
            }
        }
    }

    outfile->Printf("\n    Total Energy: %25.15f\n",nuclear_repulsion_energy + energy_1rdm + energy_2rdm);

    // Print RDM timings
    for (size_t n = 0; n < rdm_timing.size();++n){
        outfile->Printf("\n    Timing for %d-RDM: %.3f s",n + 1,rdm_timing[n]);
    }

    if (fci_debug_level > 3){
        rdm_test();
    }
}

/**
 * Compute the one-particle density matrix for a given wave function
 * @param alfa flag for alfa or beta component, true = alfa, false = beta
 */
void FCIWfn::compute_1rdm(std::vector<double>& rdm, bool alfa)
{
    rdm.assign(ncmo_ * ncmo_,0.0);

    for(int alfa_sym = 0; alfa_sym < nirrep_; ++alfa_sym){
        int beta_sym = alfa_sym ^ symmetry_;
        if(detpi_[alfa_sym] > 0){
            SharedMatrix C = alfa ? C_[alfa_sym] : C1;
            double** Ch = C->pointer();

            if(!alfa){
                C->zero();
                size_t maxIa = alfa_graph_->strpi(alfa_sym);
                size_t maxIb = beta_graph_->strpi(beta_sym);

                double** C0h = C_[alfa_sym]->pointer();

                // Copy C0 transposed in C1
                for(size_t Ia = 0; Ia < maxIa; ++Ia)
                    for(size_t Ib = 0; Ib < maxIb; ++Ib)
                        Ch[Ib][Ia] = C0h[Ia][Ib];
            }

            size_t maxL = alfa ? beta_graph_->strpi(beta_sym) : alfa_graph_->strpi(alfa_sym);

            for(int p_sym = 0; p_sym < nirrep_; ++p_sym){
                int q_sym = p_sym;  // Select the totat symmetric irrep
                for(int p_rel = 0; p_rel < cmopi_[p_sym]; ++p_rel){
                    for(int q_rel = 0; q_rel < cmopi_[q_sym]; ++q_rel){
                        int p_abs = p_rel + cmopi_offset_[p_sym];
                        int q_abs = q_rel + cmopi_offset_[q_sym];
                        std::vector<StringSubstitution>& vo = alfa ? lists_->get_alfa_vo_list(p_abs,q_abs,alfa_sym)
                                                                   : lists_->get_beta_vo_list(p_abs,q_abs,beta_sym);
                        int maxss = vo.size();
                        for(int ss = 0; ss < maxss; ++ss){
                            double H = static_cast<double>(vo[ss].sign);
                            double* y = &(Ch[vo[ss].J][0]);
                            double* c = &(Ch[vo[ss].I][0]);
                            for(size_t L = 0; L < maxL; ++L){
                                rdm[p_abs * ncmo_ + q_abs] += c[L] * y[L] * H;
                            }
                        }
                    }
                }
            }
        }
    } // End loop over h

#if 0
    outfile->Printf("\n OPDM:");
    for (int p = 0; p < ncmo_; ++p) {
        outfile->Printf("\n");
        for (int q = 0; q < ncmo_; ++q) {
            outfile->Printf("%15.12f ",rdm[oei_index(p,q)]);
        }
    }
#endif
}


/**
 * Compute the aa/bb two-particle density matrix for a given wave function
 * @param alfa flag for alfa or beta component, true = aa, false = bb
 */
void FCIWfn::compute_2rdm_aa(std::vector<double>& rdm, bool alfa)
{
    rdm.assign(ncmo_ * ncmo_ * ncmo_ * ncmo_,0.0);
    // Notation
    // ha - symmetry of alpha strings
    // hb - symmetry of beta strings
    for(int ha = 0; ha < nirrep_; ++ha){
        int hb = ha ^ symmetry_;
        if(detpi_[ha] > 0){
            SharedMatrix C = alfa ? C_[ha] : C1;
            double** Ch = C->pointer();

            if(!alfa){
                C->zero();
                size_t maxIa = alfa_graph_->strpi(ha);
                size_t maxIb = beta_graph_->strpi(hb);

                double** C0h = C_[ha]->pointer();

                // Copy C0 transposed in C1
                for(size_t Ia = 0; Ia < maxIa; ++Ia)
                    for(size_t Ib = 0; Ib < maxIb; ++Ib)
                        Ch[Ib][Ia] = C0h[Ia][Ib];
            }

            size_t maxL = alfa ? beta_graph_->strpi(hb) : alfa_graph_->strpi(ha);
            // Loop over (p>q) == (p>q)
            for(int pq_sym = 0; pq_sym < nirrep_; ++pq_sym){
                size_t max_pq = lists_->pairpi(pq_sym);
                for(size_t pq = 0; pq < max_pq; ++pq){
                    const Pair& pq_pair = lists_->get_nn_list_pair(pq_sym,pq);
                    int p_abs = pq_pair.first;
                    int q_abs = pq_pair.second;

                    std::vector<StringSubstitution>& OO = alfa ? lists_->get_alfa_oo_list(pq_sym,pq,ha)
                                                               : lists_->get_beta_oo_list(pq_sym,pq,hb);
                    double rdm_element = 0.0;
                    size_t maxss = OO.size();
                    for(size_t ss = 0; ss < maxss; ++ss){
                        double H = static_cast<double>(OO[ss].sign);
                        double* y = &(Ch[OO[ss].J][0]);
                        double* c = &(Ch[OO[ss].I][0]);
                        for(size_t L = 0; L < maxL; ++L){
                            rdm_element += c[L] * y[L] * H;
                        }
                    }

                    rdm[tei_index(p_abs,q_abs,p_abs,q_abs)] += rdm_element;
                    rdm[tei_index(p_abs,q_abs,q_abs,p_abs)] -= rdm_element;
                    rdm[tei_index(q_abs,p_abs,p_abs,q_abs)] -= rdm_element;
                    rdm[tei_index(q_abs,p_abs,q_abs,p_abs)] += rdm_element;
                }
            }
            // Loop over (p>q) > (r>s)
            for(int pq_sym = 0; pq_sym < nirrep_; ++pq_sym){
                size_t max_pq = lists_->pairpi(pq_sym);
                for(size_t pq = 0; pq < max_pq; ++pq){
                    const Pair& pq_pair = lists_->get_nn_list_pair(pq_sym,pq);
                    int p_abs = pq_pair.first;
                    int q_abs = pq_pair.second;
                    for(size_t rs = 0; rs < pq; ++rs){
                        const Pair& rs_pair = lists_->get_nn_list_pair(pq_sym,rs);
                        int r_abs = rs_pair.first;
                        int s_abs = rs_pair.second;
                        double integral = alfa ? tei_aaaa(p_abs,q_abs,r_abs,s_abs) : tei_bbbb(p_abs,q_abs,r_abs,s_abs);

                        double rdm_element = 0.0;
                        std::vector<StringSubstitution>& VVOO = alfa ? lists_->get_alfa_vvoo_list(p_abs,q_abs,r_abs,s_abs,ha)
                                                                     : lists_->get_beta_vvoo_list(p_abs,q_abs,r_abs,s_abs,hb);

                        // TODO loop in a differen way
                        size_t maxss = VVOO.size();
                        for(size_t ss = 0; ss < maxss; ++ss){
                            double H = static_cast<double>(VVOO[ss].sign);
                            double* y = &(Ch[VVOO[ss].J][0]);
                            double* c = &(Ch[VVOO[ss].I][0]);
                            for(size_t L = 0; L < maxL; ++L){
                                rdm_element += c[L] * y[L] * H;
                            }
                        }

                        rdm[tei_index(p_abs,q_abs,r_abs,s_abs)] += rdm_element;
                        rdm[tei_index(q_abs,p_abs,r_abs,s_abs)] -= rdm_element;
                        rdm[tei_index(p_abs,q_abs,s_abs,r_abs)] -= rdm_element;
                        rdm[tei_index(q_abs,p_abs,s_abs,r_abs)] += rdm_element;
                        rdm[tei_index(r_abs,s_abs,p_abs,q_abs)] += rdm_element;
                        rdm[tei_index(r_abs,s_abs,q_abs,p_abs)] -= rdm_element;
                        rdm[tei_index(s_abs,r_abs,p_abs,q_abs)] -= rdm_element;
                        rdm[tei_index(s_abs,r_abs,q_abs,p_abs)] += rdm_element;
                    }
                }
            }
        }
    } // End loop over h
#if 0
    outfile->Printf("\n TPDM:");
    for (int p = 0; p < ncmo_; ++p) {
        for (int q = 0; q <= p; ++q) {
            for (int r = 0; r < ncmo_; ++r) {
                for (int s = 0; s <= r; ++s) {
                    if (std::fabs(rdm[tei_index(p,q,r,s)]) > 1.0e-12){
                        outfile->Printf("\n  Lambda [%3lu][%3lu][%3lu][%3lu] = %18.12lf", p,q,r,s, rdm[tei_index(p,q,r,s)]);

                    }
                }
            }
        }
    }
#endif
}


/**
 * Compute the ab two-particle density matrix for a given wave function
 * @param alfa flag for alfa or beta component, true = aa, false = bb
 */
void FCIWfn::compute_2rdm_ab(std::vector<double>& rdm)
{
    rdm.assign(ncmo_ * ncmo_ * ncmo_ * ncmo_,0.0);

    // Loop over blocks of matrix C
    for(int Ia_sym = 0; Ia_sym < nirrep_; ++Ia_sym){
        size_t maxIa = alfa_graph_->strpi(Ia_sym);
        int Ib_sym = Ia_sym ^ symmetry_;
        double** C = C_[Ia_sym]->pointer();

        // Loop over all r,s
        for(int rs_sym = 0; rs_sym < nirrep_; ++rs_sym){
            int Jb_sym = Ib_sym ^ rs_sym; // <- Looks like it should fail for states with symmetry != A1  URGENT
            int Ja_sym = Jb_sym ^ symmetry_; // <- Looks like it should fail for states with symmetry != A1  URGENT
            //            int Ja_sym = Ia_sym ^ rs_sym;
            size_t maxJa = alfa_graph_->strpi(Ja_sym);
            double** Y = C_[Ja_sym]->pointer();
            for(int r_sym = 0; r_sym < nirrep_; ++r_sym){
                int s_sym = rs_sym ^ r_sym;

                for(int r_rel = 0; r_rel < cmopi_[r_sym]; ++r_rel){
                    for(int s_rel = 0; s_rel < cmopi_[s_sym]; ++s_rel){
                        int r_abs = r_rel + cmopi_offset_[r_sym];
                        int s_abs = s_rel + cmopi_offset_[s_sym];

                        // Grab list (r,s,Ib_sym)
                        std::vector<StringSubstitution>& vo_beta = lists_->get_beta_vo_list(r_abs,s_abs,Ib_sym);
                        size_t maxSSb = vo_beta.size();

                        // Loop over all p,q
                        int pq_sym = rs_sym;
                        for(int p_sym = 0; p_sym < nirrep_; ++p_sym){
                            int q_sym = pq_sym ^ p_sym;
                            for(int p_rel = 0; p_rel < cmopi_[p_sym]; ++p_rel){
                                int p_abs = p_rel + cmopi_offset_[p_sym];
                                for(int q_rel = 0; q_rel < cmopi_[q_sym]; ++q_rel){
                                    int q_abs = q_rel + cmopi_offset_[q_sym];

                                    std::vector<StringSubstitution>& vo_alfa = lists_->get_alfa_vo_list(p_abs,q_abs,Ia_sym);

                                    size_t maxSSa = vo_alfa.size();
                                    for(size_t SSa = 0; SSa < maxSSa; ++SSa){
                                        for(size_t SSb = 0; SSb < maxSSb; ++SSb){
                                            double V = static_cast<double>(vo_alfa[SSa].sign * vo_beta[SSb].sign);
                                            rdm[tei_index(p_abs,r_abs,q_abs,s_abs)] += Y[vo_alfa[SSa].J][vo_beta[SSb].J] * C[vo_alfa[SSa].I][vo_beta[SSb].I] * V;
                                        }
                                    }
                                }
                            }
                        } // End loop over p,q
                    }
                } // End loop over r_rel,s_rel
            }
        }
    }
#if 0
    outfile->Printf("\n TPDM (ab):");
    for (int p = 0; p < ncmo_; ++p) {
        for (int q = 0; q < ncmo_; ++q) {
            for (int r = 0; r < ncmo_; ++r) {
                for (int s = 0; s < ncmo_; ++s) {
                    if (std::fabs(rdm[tei_index(p,q,r,s)]) > 1.0e-12){
                        outfile->Printf("\n  Lambda [%3lu][%3lu][%3lu][%3lu] = %18.12lf", p,q,r,s, rdm[tei_index(p,q,r,s)]);

                    }
                }
            }
        }
    }
#endif
}

#if 0
void FCIWfn::compute_3rdm_aaa(std::vector<double>& rdm, bool alfa)
{
    rdm.assign(ncmo_ * ncmo_ * ncmo_ * ncmo_ * ncmo_ * ncmo_,0.0);

    for (int h_Ja = 0; h_Ja < nirrep_; ++h_Ja){
        int h_Jb = h_Ja ^ symmetry_;

        if(detpi_[h_Ja] > 0){

            size_t maxJ = alfa ? alfa_graph_->strpi(h_Ja) : beta_graph_->strpi(h_Jb);
            size_t maxL = alfa ? beta_graph_->strpi(h_Jb) : alfa_graph_->strpi(h_Ja);

            SharedMatrix C = alfa ? C_[h_Ja] : C1;
            double** Ch = C->pointer();

            if(!alfa){
                C->zero();
                size_t maxIa = alfa_graph_->strpi(h_Ja);
                size_t maxIb = beta_graph_->strpi(h_Jb);

                double** C0h = C_[h_Ja]->pointer();

                // Copy C0 transposed in C1
                for(size_t Ia = 0; Ia < maxIa; ++Ia)
                    for(size_t Ib = 0; Ib < maxIb; ++Ib)
                        Ch[Ib][Ia] = C0h[Ia][Ib];
            }

            for (size_t J = 0; J < maxJ; ++J){
                for (int h_K = 0; h_K < nirrep_; ++h_K){
                    std::vector<KHStringSubstitution>& Klist = alfa ? lists_->get_alfa_kh_list(h_Ja,J,h_K) :
                                                                      lists_->get_beta_kh_list(h_Jb,J,h_K);
                    for (const auto Kel : Klist){
                        size_t s = Kel.p;
                        size_t r = Kel.q;
                        short sr_sign = Kel.sign;
                        size_t K = Kel.J;

                        for (int h_L = 0; h_L < nirrep_; ++h_L){
                            std::vector<KHStringSubstitution>& Llist = alfa ? lists_->get_alfa_kh_list(h_K,K,h_L) :
                                                                              lists_->get_beta_kh_list(h_K,K,h_L);
                            for (const auto Lel : Llist){
                                size_t q = Lel.p;
                                size_t t = Lel.q;
                                short qt_sign = Lel.sign;
                                size_t L = Lel.J;

                                std::vector<KHStringSubstitution>& Ilist = alfa ? lists_->get_alfa_kh_list(h_L,L,h_Ja) :
                                                                                  lists_->get_beta_kh_list(h_L,L,h_Jb);
                                for (const auto Iel : Ilist){
                                    size_t p = Iel.p;
                                    size_t u = Iel.q;
                                    short pu_sign = Iel.sign;
                                    size_t I = Iel.J;

                                    double rdm_value = 0.0;

                                    double* y = &(Ch[J][0]);
                                    double* c = &(Ch[I][0]);
                                    for(size_t L = 0; L < maxL; ++L){
                                        rdm_value += c[L] * y[L];
                                    }

                                    double sign = sr_sign * qt_sign * pu_sign;
                                    rdm_value *= sign;

                                    rdm[six_index(p,q,r,s,t,u)] -= rdm_value;
                                }
                            }
                        }
                    }
                }
            }
        } // End loop over h
    }
}
#else
void FCIWfn::compute_3rdm_aaa(std::vector<double>& rdm, bool alfa)
{
    rdm.assign(ncmo_ * ncmo_ * ncmo_ * ncmo_ * ncmo_ * ncmo_,0.0);

    for (int h_K = 0; h_K < nirrep_; ++h_K){
        size_t maxK = lists_->alfa_graph_3h()->strpi(h_K);
        for (int h_I = 0; h_I < nirrep_; ++h_I){
            int h_Ib = h_I ^ symmetry_;
            int h_J = h_I;
            SharedMatrix C = alfa ? C_[h_J] : C1;
            double** Ch = C->pointer();

            if(!alfa){
                C->zero();
                size_t maxIa = alfa_graph_->strpi(h_I);
                size_t maxIb = beta_graph_->strpi(h_Ib);

                double** C0h = C_[h_I]->pointer();

                // Copy C0 transposed in C1
                for(size_t Ia = 0; Ia < maxIa; ++Ia)
                    for(size_t Ib = 0; Ib < maxIb; ++Ib)
                        Ch[Ib][Ia] = C0h[Ia][Ib];
            }

            size_t maxL = alfa ? beta_graph_->strpi(h_Ib) : alfa_graph_->strpi(h_I);

            for (size_t K = 0; K < maxK; ++K){
                std::vector<H3StringSubstitution>& Klist = alfa ? lists_->get_alfa_3h_list(h_K,K,h_I) :
                                                                  lists_->get_beta_3h_list(h_K,K,h_Ib);
                for (size_t L = 0; L < maxK; ++L){
                    std::vector<H3StringSubstitution>& Llist = alfa ? lists_->get_alfa_3h_list(h_K,L,h_J) :
                                                                      lists_->get_beta_3h_list(h_K,L,h_Ib);


                    for (auto& Kel : Klist){
                        size_t p = Kel.p;
                        size_t q = Kel.q;
                        size_t r = Kel.r;
                        size_t I = Kel.J;
                        for (auto& Lel : Llist){
                            size_t s = Lel.p;
                            size_t t = Lel.q;
                            size_t u = Lel.r;
                            short sign = Kel.sign * Lel.sign;
                            size_t J = Lel.J;

                            double* y = &(Ch[J][0]);
                            double* c = &(Ch[I][0]);
                            double rdm_value = 0.0;
                            for(size_t L = 0; L < maxL; ++L){
                                rdm_value += c[L] * y[L];
                            }

                            rdm_value *= sign;

                            rdm[six_index(p,q,r,s,t,u)] += rdm_value;
                        }
                    }
                }
            }
        }
    }
}
#endif

/**
 * Compute the aab three-particle density matrix for a given wave function
 * @param alfa flag for alfa or beta component, true = aa, false = bb
 */
void FCIWfn::compute_3rdm_aab(std::vector<double>& rdm, bool alfa)
{
    rdm.assign(ncmo_ * ncmo_ * ncmo_ * ncmo_ * ncmo_ * ncmo_,0.0);

    // Loop over blocks of matrix C
    for(int Ia_sym = 0; Ia_sym < nirrep_; ++Ia_sym){
        size_t maxIa = alfa_graph_->strpi(Ia_sym);
        int Ib_sym = Ia_sym ^ symmetry_;
        double** C = C_[Ia_sym]->pointer();

        // Loop over all r,s
        for(int rs_sym = 0; rs_sym < nirrep_; ++rs_sym){
            int Jb_sym = Ib_sym ^ rs_sym; // <- Looks like it should fail for states with symmetry != A1  URGENT
            int Ja_sym = Jb_sym ^ symmetry_; // <- Looks like it should fail for states with symmetry != A1  URGENT
            //            int Ja_sym = Ia_sym ^ rs_sym;
            size_t maxJa = alfa_graph_->strpi(Ja_sym);
            double** Y = C_[Ja_sym]->pointer();
            for(int r_sym = 0; r_sym < nirrep_; ++r_sym){
                int s_sym = rs_sym ^ r_sym;

                for(int r_rel = 0; r_rel < cmopi_[r_sym]; ++r_rel){
                    for(int s_rel = 0; s_rel < cmopi_[s_sym]; ++s_rel){
                        int r_abs = r_rel + cmopi_offset_[r_sym];
                        int s_abs = s_rel + cmopi_offset_[s_sym];

                        // Grab list (r,s,Ib_sym)
                        std::vector<StringSubstitution>& vo_beta = lists_->get_beta_vo_list(r_abs,s_abs,Ib_sym);
                        size_t maxSSb = vo_beta.size();

                        // Loop over all p,q
                        int pq_sym = rs_sym;
                        for(int p_sym = 0; p_sym < nirrep_; ++p_sym){
                            int q_sym = pq_sym ^ p_sym;
                            for(int p_rel = 0; p_rel < cmopi_[p_sym]; ++p_rel){
                                int p_abs = p_rel + cmopi_offset_[p_sym];
                                for(int q_rel = 0; q_rel < cmopi_[q_sym]; ++q_rel){
                                    int q_abs = q_rel + cmopi_offset_[q_sym];

                                    std::vector<StringSubstitution>& vo_alfa = lists_->get_alfa_vo_list(p_abs,q_abs,Ia_sym);

                                    size_t maxSSa = vo_alfa.size();
                                    for(size_t SSa = 0; SSa < maxSSa; ++SSa){
                                        for(size_t SSb = 0; SSb < maxSSb; ++SSb){
                                            double V = static_cast<double>(vo_alfa[SSa].sign * vo_beta[SSb].sign);
                                            rdm[tei_index(p_abs,r_abs,q_abs,s_abs)] += Y[vo_alfa[SSa].J][vo_beta[SSb].J] * C[vo_alfa[SSa].I][vo_beta[SSb].I] * V;
                                        }
                                    }
                                }
                            }
                        } // End loop over p,q
                    }
                } // End loop over r_rel,s_rel
            }
        }
    }
}

void FCIWfn::rdm_test()
{
    bool* Ia = new bool[ncmo_];
    bool* Ib = new bool[ncmo_];

    // Generate the strings 1111100000
    //                      { k }{n-k}

    size_t na = lists_->na();
    size_t nb = lists_->nb();

    for(int i = 0; i < ncmo_ - na; ++i) Ia[i] = false; // 0
    for(int i = ncmo_ - na; i < ncmo_; ++i) Ia[i] = true;  // 1

    for(int i = 0; i < ncmo_ - nb; ++i) Ib[i] = false; // 0
    for(int i = ncmo_ - nb; i < ncmo_; ++i) Ib[i] = true;  // 1

    std::vector<BitsetDeterminant> dets;
    std::map<BitsetDeterminant,size_t> dets_map;

    std::vector<double> C;
    std::vector<bool> a_occ(ncmo_);
    std::vector<bool> b_occ(ncmo_);

    size_t num_det = 0;
    do{
        for (int i = 0; i < ncmo_; ++i) a_occ[i] = Ia[i];
        do{
            for (int i = 0; i < ncmo_; ++i) b_occ[i] = Ib[i];
            if((alfa_graph_->sym(Ia) ^ beta_graph_->sym(Ib)) == symmetry_){
                BitsetDeterminant d(a_occ,b_occ);
                dets.push_back(d);
                double c = C_[alfa_graph_->sym(Ia)]->get(alfa_graph_->rel_add(Ia),beta_graph_->rel_add(Ib));
                C.push_back(c);
                dets_map[d] = num_det;
                num_det++;
            }
        } while (std::next_permutation(Ib,Ib + ncmo_));
    } while (std::next_permutation(Ia,Ia + ncmo_));


    BitsetDeterminant I;
#if 0
    double error_2rdm_aa = 0.0;
    for (size_t p = 0; p < ncmo_; ++p){
        for (size_t q = 0; q < ncmo_; ++q){
            for (size_t r = 0; r < ncmo_; ++r){
                for (size_t s = 0; s < ncmo_; ++s){
                    double rdm = 0.0;
                    for (size_t i = 0; i < dets.size(); ++i){
                        I.copy(dets[i]);
                        double sign = 1.0;
                        sign *= I.destroy_alfa_bit(r);
                        sign *= I.destroy_alfa_bit(s);
                        sign *= I.create_alfa_bit(q);
                        sign *= I.create_alfa_bit(p);
                        if (sign != 0){
                            if (dets_map.count(I) != 0){
                                rdm += sign * C[i] * C[dets_map[I]];
                            }
                        }
                    }
                    if (std::fabs(rdm) > 1.0e-12){
                        outfile->Printf("\n  D2(aaaa)[%3lu][%3lu][%3lu][%3lu] = %18.12lf (%18.12lf,%18.12lf)", p,q,r,s,rdm-tpdm_aa_[tei_index(p,q,r,s)],rdm,tpdm_aa_[tei_index(p,q,r,s)]);
                        error_2rdm_aa += std::fabs(rdm-tpdm_aa_[tei_index(p,q,r,s)]);
                    }
                }
            }
        }
    }
    outfile->Printf("\n\n  Error in AAAA 2-RDM: %f",error_2rdm_aa);
#endif

#if 0
    outfile->Printf("\n\n  ABAB 2-RDM");
    double error_2rdm_ab = 0.0;
    for (size_t p = 0; p < ncmo_; ++p){
        for (size_t q = 0; q < ncmo_; ++q){
            for (size_t r = 0; r < ncmo_; ++r){
                for (size_t s = 0; s < ncmo_; ++s){
                    double rdm = 0.0;
                    for (size_t i = 0; i < dets.size(); ++i){
                        I.copy(dets[i]);
                        double sign = 1.0;
                        sign *= I.destroy_alfa_bit(r);
                        sign *= I.destroy_beta_bit(s);
                        sign *= I.create_beta_bit(q);
                        sign *= I.create_alfa_bit(p);
                        if (sign != 0){
                            if (dets_map.count(I) != 0){
                                rdm += sign * C[i] * C[dets_map[I]];
                            }
                        }
                    }
                    if (std::fabs(rdm) > 1.0e-12){
                        outfile->Printf("\n  D2(abab)[%3lu][%3lu][%3lu][%3lu] = %18.12lf (%18.12lf,%18.12lf)", p,q,r,s,rdm-tpdm_ab_[tei_index(p,q,r,s)],rdm,tpdm_ab_[tei_index(p,q,r,s)]);
                        error_2rdm_ab += std::fabs(rdm-tpdm_ab_[tei_index(p,q,r,s)]);
                    }
                }
            }
        }
    }
    outfile->Printf("\n  Error in ABAB 2-RDM: %f",error_2rdm_ab);
#endif

#if 1
    outfile->Printf("\n\n  AABAAB 3-RDM");
    double error_3rdm_aab = 0.0;
    for (size_t p = 0; p < ncmo_; ++p){
        for (size_t q = p + 1; q < ncmo_; ++q){
            for (size_t r = 0; r < ncmo_; ++r){
                for (size_t s = 0; s < ncmo_; ++s){
                    for (size_t t = s + 1; t < ncmo_; ++t){
                        for (size_t a = 0; a < ncmo_; ++a){
                            double rdm = 0.0;
                            for (size_t i = 0; i < dets.size(); ++i){
                                I.copy(dets[i]);
                                double sign = 1.0;
                                sign *= I.destroy_alfa_bit(s);
                                sign *= I.destroy_alfa_bit(t);
                                sign *= I.destroy_beta_bit(a);
                                sign *= I.create_beta_bit(r);
                                sign *= I.create_alfa_bit(q);
                                sign *= I.create_alfa_bit(p);
                                if (sign != 0){
                                    if (dets_map.count(I) != 0){
                                        rdm += sign * C[i] * C[dets_map[I]];
                                    }
                                }
                            }
                            if (std::fabs(rdm) > 1.0e-12){
                                size_t index = six_index(p,q,r,s,t,a);
                                double rdm_comp = tpdm_aab_[index];
                                outfile->Printf("\n  D3(aabaab)[%3lu][%3lu][%3lu][%3lu][%3lu][%3lu] = %18.12lf (%18.12lf,%18.12lf)",
                                                p,q,r,s,t,a,rdm-rdm_comp,rdm,rdm_comp);
                                error_3rdm_aab += std::fabs(rdm-tpdm_aab_[six_index(p,q,r,s,t,a)]);
                            }
                        }
                    }
                }
            }
        }
    }
    outfile->Printf("\n  Error in AABAAB 3-RDM: %f",error_3rdm_aab);
#endif

#if 0
    outfile->Printf("\n\n  ABBABB 3-RDM");
    double error_3rdm_abb = 0.0;
    for (size_t p = 0; p < ncmo_; ++p){
        for (size_t q = p + 1; q < ncmo_; ++q){
            for (size_t r = 0; r < ncmo_; ++r){
                for (size_t s = 0; s < ncmo_; ++s){
                    for (size_t t = s + 1; t < ncmo_; ++t){
                        for (size_t a = 0; a < ncmo_; ++a){
                            double rdm = 0.0;
                            for (size_t i = 0; i < dets.size(); ++i){
                                I.copy(dets[i]);
                                double sign = 1.0;
                                sign *= I.destroy_alfa_bit(s);
                                sign *= I.destroy_beta_bit(t);
                                sign *= I.destroy_beta_bit(a);
                                sign *= I.create_beta_bit(r);
                                sign *= I.create_beta_bit(q);
                                sign *= I.create_alfa_bit(p);
                                if (sign != 0){
                                    if (dets_map.count(I) != 0){
                                        rdm += sign * C[i] * C[dets_map[I]];
                                    }
                                }
                            }
                            if (std::fabs(rdm) > 1.0e-12){
                                size_t index = six_index(p,q,r,s,t,a);
                                double rdm_comp = tpdm_aab_[index];
                                outfile->Printf("\n  D3(abbabb)[%3lu][%3lu][%3lu][%3lu][%3lu][%3lu] = %18.12lf (%18.12lf,%18.12lf)",
                                                p,q,r,s,t,a,rdm-rdm_comp,rdm,rdm_comp);
                                error_3rdm_abb += std::fabs(rdm-tpdm_aab_[six_index(p,q,r,s,t,a)]);
                            }
                        }
                    }
                }
            }
        }
    }
    outfile->Printf("\n  Error in AABAAB 3-RDM: %f",error_3rdm_abb);
#endif

#if 0
    outfile->Printf("\n\n  AAAAAA 3-RDM");
    double error_3rdm_aaa = 0.0;
    for (size_t p = 0; p < ncmo_; ++p){
        for (size_t q = p + 1; q < ncmo_; ++q){
            for (size_t r = q + 1; r < ncmo_; ++r){
                for (size_t s = 0; s < ncmo_; ++s){
                    for (size_t t = s + 1; t < ncmo_; ++t){
                        for (size_t a = t + 1; a < ncmo_; ++a){
                            double rdm = 0.0;
                            for (size_t i = 0; i < dets.size(); ++i){
                                I.copy(dets[i]);
                                double sign = 1.0;
                                sign *= I.destroy_alfa_bit(s);
                                sign *= I.destroy_alfa_bit(t);
                                sign *= I.destroy_alfa_bit(a);
                                sign *= I.create_alfa_bit(r);
                                sign *= I.create_alfa_bit(q);
                                sign *= I.create_alfa_bit(p);
                                if (sign != 0){
                                    if (dets_map.count(I) != 0){
                                        rdm += sign * C[i] * C[dets_map[I]];
                                    }
                                }
                            }
                            if (std::fabs(rdm) > 1.0e-12){
                                size_t index = six_index(p,q,r,s,t,a);
                                double rdm_comp = tpdm_aaa_[index];
//                                outfile->Printf("\n  D3(aaaaaa)[%3lu][%3lu][%3lu][%3lu][%3lu][%3lu] = %18.12lf (%18.12lf,%18.12lf)",
//                                                p,q,r,s,t,a,rdm-rdm_comp,rdm,rdm_comp);
                                error_3rdm_aaa += std::fabs(rdm-rdm_comp);
                            }
                        }
                    }
                }
            }
        }
    }
    outfile->Printf("\n  Error in AAAAAA 3-RDM: %f",error_3rdm_aaa);
#endif


#if 0
    outfile->Printf("\n\n  BBBBBB 3-RDM");
    double error_3rdm_bbb = 0.0;
    for (size_t p = 0; p < ncmo_; ++p){
        for (size_t q = p + 1; q < ncmo_; ++q){
            for (size_t r = q + 1; r < ncmo_; ++r){
                for (size_t s = 0; s < ncmo_; ++s){
                    for (size_t t = s + 1; t < ncmo_; ++t){
                        for (size_t a = t + 1; a < ncmo_; ++a){
                            double rdm = 0.0;
                            for (size_t i = 0; i < dets.size(); ++i){
                                I.copy(dets[i]);
                                double sign = 1.0;
                                sign *= I.destroy_beta_bit(s);
                                sign *= I.destroy_beta_bit(t);
                                sign *= I.destroy_beta_bit(a);
                                sign *= I.create_beta_bit(r);
                                sign *= I.create_beta_bit(q);
                                sign *= I.create_beta_bit(p);
                                if (sign != 0){
                                    if (dets_map.count(I) != 0){
                                        rdm += sign * C[i] * C[dets_map[I]];
                                    }
                                }
                            }
                            if (std::fabs(rdm) > 1.0e-12){
                                size_t index = six_index(p,q,r,s,t,a);
                                double rdm_comp = tpdm_bbb_[index];
//                                outfile->Printf("\n  D3(bbbbbb)[%3lu][%3lu][%3lu][%3lu][%3lu][%3lu] = %18.12lf (%18.12lf,%18.12lf)",
//                                                p,q,r,s,t,a,rdm-rdm_comp,rdm,rdm_comp);
                                error_3rdm_aaa += std::fabs(rdm-rdm_comp);
                            }
                        }
                    }
                }
            }
        }
    }
    outfile->Printf("\n  Error in BBBBBB 3-RDM: %f",error_3rdm_aaa);
#endif
    delete[] Ia;
    delete[] Ib;
}

}}
