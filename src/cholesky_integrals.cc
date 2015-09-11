#include "integrals.h"
#include <cmath>

#include <psifiles.h>
#include <libiwl/iwl.h>
#include <libmints/matrix.h>
#include <libmints/basisset.h>
#include <libmints/twobody.h>
#include <libthce/thce.h>
#include <libmints/typedefs.h>
#include <libmints/eri.h>
#include <libmints/integral.h>
#include <libmints/wavefunction.h>
#include <lib3index/cholesky.h>
#include <libqt/qt.h>
#include <algorithm>
#include <numeric>
#include "blockedtensorfactory.h"

using namespace ambit;

/**
     * @brief CholeskyIntegrals::CholeskyIntegrals
     * @param options - psi options class
     * @param restricted - type of integral transformation
     * @param resort_frozen_core -
     */
namespace psi{ namespace forte{

CholeskyIntegrals::CholeskyIntegrals(psi::Options &options, IntegralSpinRestriction restricted, IntegralFrozenCore resort_frozen_core,
std::shared_ptr<MOSpaceInfo> mo_space_info)
    : ForteIntegrals(options, restricted, resort_frozen_core, mo_space_info){
    integral_type_ = Cholesky;
    outfile->Printf("\n Cholesky integrals time");
    Timer CholInt;
    allocate();
    gather_integrals();
    make_diagonal_integrals();
    if (ncmo_ < nmo_){
        freeze_core_orbitals();
        // Set the new value of the number of orbitals to be used in indexing routines
        aptei_idx_ = ncmo_;
    }
    outfile->Printf("\n CholeskyIntegrals take %8.8f", CholInt.get());
}

CholeskyIntegrals::~CholeskyIntegrals()
{
    deallocate();
}
void CholeskyIntegrals::make_diagonal_integrals()
{
    for(size_t p = 0; p < nmo_; ++p){
        for(size_t q = 0; q < nmo_; ++q){
            diagonal_aphys_tei_aa[p * nmo_ + q] = aptei_aa(p,q,p,q);
            diagonal_aphys_tei_ab[p * nmo_ + q] = aptei_ab(p,q,p,q);
            diagonal_aphys_tei_bb[p * nmo_ + q] = aptei_bb(p,q,p,q);
        }
    }
}

double CholeskyIntegrals::aptei_aa(size_t p, size_t q, size_t r, size_t s)
{
    double vpqrsalphaC = 0.0;
    double vpqrsalphaE = 0.0;
    vpqrsalphaC = C_DDOT(nthree_,
            &(ThreeIntegral_->pointer()[p*aptei_idx_ + r][0]),1,
            &(ThreeIntegral_->pointer()[q*aptei_idx_ + s][0]),1);
     vpqrsalphaE = C_DDOT(nthree_,
            &(ThreeIntegral_->pointer()[p*aptei_idx_ + s][0]),1,
            &(ThreeIntegral_->pointer()[q*aptei_idx_ + r][0]),1);

    return (vpqrsalphaC - vpqrsalphaE);

}

double CholeskyIntegrals::aptei_ab(size_t p, size_t q, size_t r, size_t s)
{
    double vpqrsalphaC = 0.0;
    vpqrsalphaC = C_DDOT(nthree_,
            &(ThreeIntegral_->pointer()[p*aptei_idx_ + r][0]),1 ,
            &(ThreeIntegral_->pointer()[q*aptei_idx_ + s][0]),1 );
    return (vpqrsalphaC);
}

double CholeskyIntegrals::aptei_bb(size_t p, size_t q, size_t r, size_t s)
{
    double vpqrsalphaC = 0.0, vpqrsalphaE = 0.0;
    vpqrsalphaC = C_DDOT(nthree_,
            &(ThreeIntegral_->pointer()[p*aptei_idx_ + r][0]),1,
            &(ThreeIntegral_->pointer()[q*aptei_idx_ + s][0]),1);
     vpqrsalphaE = C_DDOT(nthree_,
            &(ThreeIntegral_->pointer()[p*aptei_idx_ + s][0]),1,
            &(ThreeIntegral_->pointer()[q*aptei_idx_ + r][0]),1);
    //}

    return (vpqrsalphaC - vpqrsalphaE);
}
ambit::Tensor CholeskyIntegrals::aptei_aa_block(const std::vector<size_t>& p, const std::vector<size_t>& q, const std::vector<size_t>& r,
    const std::vector<size_t> & s)
{
    ambit::Tensor ReturnTensor = ambit::Tensor::build(tensor_type_,"Return",{p.size(),q.size(), r.size(), s.size()});
    ReturnTensor.iterate([&](const std::vector<size_t>& i,double& value){
        value = aptei_aa(p[i[0]], q[i[1]], r[i[2]], s[i[3]]);
    });
    return ReturnTensor;
}

ambit::Tensor CholeskyIntegrals::aptei_ab_block(const std::vector<size_t>& p, const std::vector<size_t>& q, const std::vector<size_t>& r,
    const std::vector<size_t> & s)
{
    ambit::Tensor ReturnTensor = ambit::Tensor::build(tensor_type_,"Return",{p.size(),q.size(), r.size(), s.size()});
    ReturnTensor.iterate([&](const std::vector<size_t>& i,double& value){
        value = aptei_ab(p[i[0]], q[i[1]], r[i[2]], s[i[3]]);
    });
    return ReturnTensor;
}

ambit::Tensor CholeskyIntegrals::aptei_bb_block(const std::vector<size_t>& p, const std::vector<size_t>& q, const std::vector<size_t>& r,
    const std::vector<size_t> & s)
{
    ambit::Tensor ReturnTensor = ambit::Tensor::build(tensor_type_,"Return",{p.size(),q.size(), r.size(), s.size()});
    ReturnTensor.iterate([&](const std::vector<size_t>& i,double& value){
        value = aptei_bb(p[i[0]], q[i[1]], r[i[2]], s[i[3]]);
    });
    return ReturnTensor;
}
ambit::Tensor CholeskyIntegrals::three_integral_block(const std::vector<size_t> &A, const std::vector<size_t> &p, const std::vector<size_t> &q)
{
    ambit::Tensor ReturnTensor = ambit::Tensor::build(tensor_type_,"Return",{A.size(), p.size(), q.size()});
    ReturnTensor.iterate([&](const std::vector<size_t>& i,double& value){
        value = three_integral(A[i[0]], p[i[1]], q[i[2]]);
    });
    return ReturnTensor;
}

void CholeskyIntegrals::gather_integrals()
{
    outfile->Printf("\n Computing the Cholesky Vectors \n");


    boost::shared_ptr<Wavefunction> wfn = Process::environment.wavefunction();
    boost::shared_ptr<BasisSet> primary = wfn->basisset();


    size_t nbf = primary->nbf();


    boost::shared_ptr<IntegralFactory> integral(new IntegralFactory(primary, primary, primary, primary));
    double tol_cd = options_.get_double("CHOLESKY_TOLERANCE");

    //This is creates the cholesky decomposed AO integrals
    Timer timer;
    std::string str= "Computing CD Integrals";
    outfile->Printf("\n    %-36s ...", str.c_str());
    boost::shared_ptr<CholeskyERI> Ch (new CholeskyERI(boost::shared_ptr<TwoBodyAOInt>(integral->eri()),0.0 ,tol_cd, Process::environment.get_memory()));
    //Computes the cholesky integrals
    Ch->choleskify();
    outfile->Printf("...Done. Timing %15.6f s", timer.get());

    //The number of vectors required to do cholesky factorization
    size_t nL = Ch->Q();
    nthree_ = nL;
    outfile->Printf("\n Need %8.6f GB to store cd integrals in core\n",nL * nbf * nbf * sizeof(double) / 1073741824.0 );
    int_mem_ = (nL * nbf * nbf * sizeof(double) / 1073741824.0);

    TensorType tensor_type = kCore;

    outfile->Printf("\n Number of cholesky vectors %d to satisfy %20.12f tolerance\n", nL,tol_cd);
    SharedMatrix Lao = Ch->L();
    SharedMatrix L(new Matrix("Lmo", nL, (nmo_)*(nmo_)));
    SharedMatrix Ca_ao(new Matrix("Ca_ao",nso_,nmopi_.sum()));
    SharedMatrix Ca = wfn->Ca();
    SharedMatrix aotoso = wfn->aotoso();

    // Transform from the SO to the AO basis
    Dimension nsopi_ = wfn->nsopi();
    for (int h = 0, index = 0; h < nirrep_; ++h){
        for (int i = 0; i < nmopi_[h]; ++i){
            int nao = nso_;
            int nso = nsopi_[h];

            if (!nso) continue;

            C_DGEMV('N',nao,nso,1.0,aotoso->pointer(h)[0],nso,&Ca->pointer(h)[0][i],nmopi_[h],0.0,&Ca_ao->pointer()[0][index],nmopi_.sum());

            index += 1;
        }

    }

    ambit::Tensor ThreeIntegral_ao = ambit::Tensor::build(tensor_type,"ThreeIndex",{nthree_,nmo_, nmo_ });
    ambit::Tensor Cpq_tensor = ambit::Tensor::build(tensor_type,"C_sorted",{nbf,nmo_});
    ambit::Tensor ThreeIntegral = ambit::Tensor::build(tensor_type,"ThreeIndex",{nthree_,nmo_, nmo_ });

    Cpq_tensor.iterate([&](const std::vector<size_t>& i,double& value){
        value = Ca_ao->get(i[0],i[1]);
    });
    ThreeIntegral_ao.iterate([&](const std::vector<size_t>& i,double& value){
        value = Lao->get(i[0],i[1]*nbf + i[2]);
    });
    SharedMatrix ThreeInt(new Matrix("Lmo", (nmo_)*(nmo_), nL));
    ThreeIntegral_ =ThreeInt;


    ThreeIntegral("L,p,q") = ThreeIntegral_ao("L,m,n")*Cpq_tensor("m,p")*Cpq_tensor("n,q");

    ThreeIntegral.iterate([&](const std::vector<size_t>& i,double& value){
        ThreeIntegral_->set(i[1]*nmo_ + i[2],i[0],value);
    });
}

void CholeskyIntegrals::allocate()
{
    // Allocate the memory required to store the one-electron integrals

    // Allocate the memory required to store the two-electron integrals
    diagonal_aphys_tei_aa = new double[nmo_ * nmo_];
    diagonal_aphys_tei_ab = new double[nmo_ * nmo_];
    diagonal_aphys_tei_bb = new double[nmo_ * nmo_];

}

void CholeskyIntegrals::update_integrals(bool freeze_core)
{
    make_diagonal_integrals();
    if (freeze_core){
        if (ncmo_ < nmo_){
            freeze_core_orbitals();
            aptei_idx_ = ncmo_;
        }
    }
}

void CholeskyIntegrals::retransform_integrals()
{
    aptei_idx_ = nmo_;
    transform_one_electron_integrals();
    gather_integrals();
    update_integrals();
}

void CholeskyIntegrals::deallocate()
{

    // Deallocate the memory required to store the one-electron integrals

    delete[] diagonal_aphys_tei_aa;
    delete[] diagonal_aphys_tei_ab;
    delete[] diagonal_aphys_tei_bb;

    //delete[] qt_pitzer_;
}

void CholeskyIntegrals::make_fock_matrix(SharedMatrix gamma_aM,SharedMatrix gamma_bM)
{
    TensorType tensor_type = kCore;
    ambit::Tensor ThreeIntegralTensor = ambit::Tensor::build(tensor_type,"ThreeIndex",{nthree_,ncmo_, ncmo_ });
    ambit::Tensor gamma_a = ambit::Tensor::build(tensor_type, "Gamma_a",{ncmo_, ncmo_});
    ambit::Tensor gamma_b = ambit::Tensor::build(tensor_type, "Gamma_b",{ncmo_, ncmo_});
    ambit::Tensor fock_a = ambit::Tensor::build(tensor_type, "Fock_a",{ncmo_, ncmo_});
    ambit::Tensor fock_b = ambit::Tensor::build(tensor_type, "Fock_b",{ncmo_, ncmo_});

    ThreeIntegralTensor.iterate([&](const std::vector<size_t>& i,double& value){
        value = ThreeIntegral_->get(i[1]*aptei_idx_ + i[2], i[0]);
    });
    gamma_a.iterate([&](const std::vector<size_t>& i,double& value){
        value = gamma_aM->get(i[0],i[1]);
    });
    gamma_b.iterate([&](const std::vector<size_t>& i,double& value){
        value = gamma_bM->get(i[0],i[1]);
    });

    fock_a.iterate([&](const std::vector<size_t>& i,double& value){
        value = one_electron_integrals_a[i[0] * aptei_idx_ + i[1]];
    });

    fock_b.iterate([&](const std::vector<size_t>& i,double& value){
        value = one_electron_integrals_b[i[0] * aptei_idx_ + i[1]];
    });


    fock_a("p,q") +=  ThreeIntegralTensor("Q,p,q") * ThreeIntegralTensor("Q,r,s") * gamma_a("r,s");
    fock_a("p,q") -= ThreeIntegralTensor("Q,p,r") * ThreeIntegralTensor("Q,q,s") * gamma_a("r,s");
    fock_a("p,q") +=  ThreeIntegralTensor("Q,p,q") * ThreeIntegralTensor("Q,r,s") * gamma_b("r,s");

    fock_b("p,q") +=  ThreeIntegralTensor("Q,p,q") * ThreeIntegralTensor("Q,r,s") * gamma_b("r,s");
    fock_b("p,q") -= ThreeIntegralTensor("Q,p,r") * ThreeIntegralTensor("Q,q,s") * gamma_b("r,s");
    fock_b("p,q") +=  ThreeIntegralTensor("Q,p,q") * ThreeIntegralTensor("Q,r,s") * gamma_a("r,s");

    fock_a.iterate([&](const std::vector<size_t>& i,double& value){
        fock_matrix_a[i[0] * aptei_idx_ + i[1]] = value;
    });
    fock_b.iterate([&](const std::vector<size_t>& i,double& value){
        fock_matrix_b[i[0] * aptei_idx_ + i[1]] = value;
    });
}

void CholeskyIntegrals::make_fock_matrix(bool* Ia, bool* Ib)
{
    for(size_t p = 0; p < ncmo_; ++p){
        for(size_t q = 0; q < ncmo_; ++q){
            // Builf Fock Diagonal alpha-alpha
            fock_matrix_a[p * ncmo_ + q] = oei_a(p,q);
            // Add the non-frozen alfa part, the forzen core part is already included in oei
            for (int k = 0; k < ncmo_; ++k) {
                if (Ia[k]) {
                    fock_matrix_a[p * ncmo_ + q] += aptei_aa(p,k,q,k);
                }
                if (Ib[k]) {
                    fock_matrix_a[p * ncmo_ + q] += aptei_ab(p,k,q,k);
                }
            }
            fock_matrix_b[p * ncmo_ + q] = oei_b(p,q);
            // Add the non-frozen alfa part, the forzen core part is already included in oei
            for (int k = 0; k < ncmo_; ++k) {
                if (Ib[k]) {
                    fock_matrix_b[p * ncmo_ + q] += aptei_bb(p,k,q,k);
                }
                if (Ia[k]) {
                    fock_matrix_b[p * ncmo_ + q] += aptei_ab(p,k,q,k);
                }
            }
        }
    }
}

void CholeskyIntegrals::make_fock_matrix(const boost::dynamic_bitset<>& Ia,const boost::dynamic_bitset<>& Ib)
{
    for(size_t p = 0; p < ncmo_; ++p){
        for(size_t q = p; q < ncmo_; ++q){
            // Builf Fock Diagonal alpha-alpha
            double fock_a_pq = oei_a(p,q);
            //            fock_matrix_a[p * ncmo_ + q] = oei_a(p,q);
            // Add the non-frozen alfa part, the forzen core part is already included in oei
            for (int k = 0; k < ncmo_; ++k) {
                if (Ia[k]) {
                    fock_a_pq += aptei_aa(p,k,q,k);
                }
                if (Ib[k]) {
                    fock_a_pq += aptei_ab(p,k,q,k);
                }
            }
            fock_matrix_a[p * ncmo_ + q] = fock_matrix_a[q * ncmo_ + p] = fock_a_pq;
            double fock_b_pq = oei_b(p,q);
            // Add the non-frozen alfa part, the forzen core part is already included in oei
            for (int k = 0; k < ncmo_; ++k) {
                if (Ib[k]) {
                    fock_b_pq += aptei_bb(p,k,q,k);
                }
                if (Ia[k]) {
                    fock_b_pq += aptei_ab(p,k,q,k);
                }
            }
            fock_matrix_b[p * ncmo_ + q] = fock_matrix_b[q * ncmo_ + p] = fock_b_pq;
        }
    }
}

void CholeskyIntegrals::make_fock_diagonal(bool* Ia, bool* Ib, std::pair<std::vector<double>, std::vector<double> > &fock_diagonals)
{
    std::vector<double>& fock_diagonal_alpha = fock_diagonals.first;
    std::vector<double>& fock_diagonal_beta = fock_diagonals.second;
    for(size_t p = 0; p < ncmo_; ++p){
        // Builf Fock Diagonal alpha-alpha
        fock_diagonal_alpha[p] =  oei_a(p,p);// roei(p,p);
        // Add the non-frozen alfa part, the forzen core part is already included in oei
        for (int k = 0; k < ncmo_; ++k) {
            if (Ia[k]) {
                //                fock_diagonal_alpha[p] += diag_ce_rtei(p,k); //rtei(p,p,k,k) - rtei(p,k,p,k);
                fock_diagonal_alpha[p] += diag_aptei_aa(p,k); //rtei(p,p,k,k) - rtei(p,k,p,k);
            }
            if (Ib[k]) {
                //                fock_diagonal_alpha[p] += diag_c_rtei(p,k); //rtei(p,p,k,k);
                fock_diagonal_alpha[p] += diag_aptei_ab(p,k); //rtei(p,p,k,k);
            }
        }
        fock_diagonal_beta[p] =  oei_b(p,p);
        // Add the non-frozen alfa part, the forzen core part is already included in oei
        for (int k = 0; k < ncmo_; ++k) {
            if (Ib[k]) {
                //                fock_diagonal_beta[p] += diag_ce_rtei(p,k); //rtei(p,p,k,k) - rtei(p,k,p,k);
                fock_diagonal_beta[p] += diag_aptei_bb(p,k); //rtei(p,p,k,k) - rtei(p,k,p,k);
            }
            if (Ia[k]) {
                //                fock_diagonal_beta[p] += diag_c_rtei(p,k); //rtei(p,p,k,k);
                fock_diagonal_beta[p] += diag_aptei_ab(p,k); //rtei(p,p,k,k);
            }
        }
    }
}

void CholeskyIntegrals::make_alpha_fock_diagonal(bool* Ia, bool* Ib,std::vector<double> &fock_diagonal)
{
    for(size_t p = 0; p < ncmo_; ++p){
        // Builf Fock Diagonal alpha-alpha
        fock_diagonal[p] = oei_a(p,p);
        // Add the non-frozen alfa part, the forzen core part is already included in oei
        for (int k = 0; k < ncmo_; ++k) {
            if (Ia[k]) {
                fock_diagonal[p] += diag_aptei_aa(p,k);  //diag_ce_rtei(p,k); //rtei(p,p,k,k) - rtei(p,k,p,k);
            }
            if (Ib[k]) {
                fock_diagonal[p] += diag_aptei_ab(p,k); // diag_c_rtei(p,k); //rtei(p,p,k,k);
            }
        }
    }
}

void CholeskyIntegrals::make_beta_fock_diagonal(bool* Ia, bool* Ib, std::vector<double> &fock_diagonals)
{
    for(size_t p = 0; p < ncmo_; ++p){
        fock_diagonals[p] = oei_b(p,p);
        // Add the non-frozen alfa part, the forzen core part is already included in oei
        for (int k = 0; k < ncmo_; ++k) {
            if (Ia[k]) {
                fock_diagonals[p] += diag_aptei_ab(p,k);  //diag_c_rtei(p,k); //rtei(p,p,k,k);
            }
            if (Ib[k]) {
                fock_diagonals[p] += diag_aptei_bb(p,k);  //diag_ce_rtei(p,k); //rtei(p,p,k,k) - rtei(p,k,p,k);
            }
        }
    }
}

void CholeskyIntegrals::resort_integrals_after_freezing()
{
    outfile->Printf("\n  Resorting integrals after freezing core.");

    // Create an array that maps the CMOs to the MOs (cmo2mo).
    std::vector<size_t> cmo2mo;
    for (int h = 0, q = 0; h < nirrep_; ++h){
        q += frzcpi_[h]; // skip the frozen core
        for (int r = 0; r < ncmopi_[h]; ++r){
            cmo2mo.push_back(q);
            q++;
        }
        q += frzvpi_[h]; // skip the frozen virtual
    }
    cmotomo_ = (cmo2mo);

    // Resort the integrals
    resort_two(one_electron_integrals_a,cmo2mo);
    resort_two(one_electron_integrals_b,cmo2mo);
    resort_two(diagonal_aphys_tei_aa,cmo2mo);
    resort_two(diagonal_aphys_tei_ab,cmo2mo);
    resort_two(diagonal_aphys_tei_bb,cmo2mo);

    resort_three(ThreeIntegral_,cmo2mo);

}
void CholeskyIntegrals::resort_three(boost::shared_ptr<Matrix>& threeint,std::vector<size_t>& map)
{
    //Create a temperature threeint matrix
    SharedMatrix temp_threeint(threeint->clone());
    temp_threeint->zero();

    // Borrwed from resort_four.
    // Since L is not sorted, only need to sort the columns
    // Surprisingly, this was pretty easy.
    for (size_t L = 0; L < nthree_; ++L){
        for (size_t q = 0; q < ncmo_; ++q){
            for (size_t r = 0; r < ncmo_; ++r){
                size_t Lpq_cmo  = q * ncmo_ + r;
                size_t Lpq_mo  = map[q] * nmo_ + map[r];
                temp_threeint->set(Lpq_cmo, L, threeint->get(Lpq_mo, L));

            }
        }
    }

    //This copies the resorted integrals and the data is changed to the sorted
    //matrix
    threeint->copy(temp_threeint);
}

void CholeskyIntegrals::set_tei(size_t p, size_t q, size_t r, size_t s, double value, bool alpha1, bool alpha2)
{
    outfile->Printf("\n If you are using this, you are ruining the advantages of DF/CD");
    throw PSIEXCEPTION("Don't use DF/CD if you use set_tei");
}

void CholeskyIntegrals::freeze_core_orbitals()
{
    compute_frozen_core_energy();
    compute_frozen_one_body_operator();
    if (resort_frozen_core_ == RemoveFrozenMOs){
        resort_integrals_after_freezing();
    }
}

void CholeskyIntegrals::compute_frozen_core_energy()
{
    frozen_core_energy_ = 0.0;

    for (int hi = 0, p = 0; hi < nirrep_; ++hi){
        for (int i = 0; i < frzcpi_[hi]; ++i){
            frozen_core_energy_ += oei_a(p + i,p + i) + oei_b(p + i,p + i);

            for (int hj = 0, q = 0; hj < nirrep_; ++hj){
                for (int j = 0; j < frzcpi_[hj]; ++j){
                    frozen_core_energy_ += 0.5 * diag_aptei_aa(p + i,q + j) + 0.5 * diag_aptei_bb(p + i,q + j) + diag_aptei_ab(p + i,q + j);
                }
                q += nmopi_[hj]; // orbital offset for the irrep hj
            }
        }
        p += nmopi_[hi]; // orbital offset for the irrep hi
    }
    outfile->Printf("\n  Frozen-core energy        %20.12f a.u.",frozen_core_energy_);
}

void CholeskyIntegrals::compute_frozen_one_body_operator()
{
    Timer FrozenOneBody;
    boost::shared_ptr<BlockedTensorFactory>BTF(new BlockedTensorFactory(options_));

    size_t f = 0; // The Offset for irrep
    size_t r = 0; // The MO number for frozen core
    size_t g = 0; //the Offset for irrep
    std::vector<size_t> frozen_vec;
    std::vector<size_t> corrleated_vec;

    for (size_t hi = 0; hi < nirrep_; ++hi){
        for (size_t i = 0; i < frzcpi_[hi]; ++i){
            r = f + i;
            frozen_vec.push_back(r);
        }
        f += nmopi_[hi];
        for (size_t p = frzcpi_[hi]; p < nmopi_[hi]; p++)
        {
            size_t mo = p + g;
            corrleated_vec.push_back(mo);
        }
        g += nmopi_[hi];
    }
    //Get the size of frozen MO
    size_t frozen_size = frozen_vec.size();

    //Form a map that says mo_to_rel[ABS_MO] =relative in frozen array
    std::map<size_t, size_t>  mo_to_rel;
    std::vector<size_t> motofrozen(frozen_size);
    std::iota(motofrozen.begin(), motofrozen.end(), 0);

    int i = 0;
    for (auto frozen : frozen_vec)
    {
        mo_to_rel[frozen] = motofrozen[i];
        i++;
    }

    BTF->add_mo_space("f","rs", frozen_vec, NoSpin);
    BTF->add_mo_space("k", "mn", corrleated_vec, NoSpin);
    BTF->add_composite_mo_space("a", "pq", {"f", "k"});

    std::vector<size_t> nauxpi(nthree_);
    std::iota(nauxpi.begin(), nauxpi.end(),0);

    BTF->add_mo_space("d","g",nauxpi,NoSpin);

    //Kevin is lazy.  Going to use ambit to perform this contraction
    ambit::BlockedTensor ThreeIntegral = BTF->build(kCore,"ThreeInt",{"daa"});
    ambit::BlockedTensor FullFrozenV   = BTF->build(kCore, "FullFrozenV", {"ffaa"});
    ambit::BlockedTensor FullFrozenVAB   = BTF->build(kCore, "FullFrozenV", {"ffaa"});

    ThreeIntegral.iterate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,double& value){
        value = three_integral(i[0], i[1], i[2]);
    });
    boost::shared_ptr<Matrix> FrozenVMatrix(new Matrix("FrozenV", frozen_size * frozen_size, nmo_ *  nmo_));
    boost::shared_ptr<Matrix> FrozenVMatrixAB(new Matrix("FrozenVAB", frozen_size * frozen_size, nmo_ * nmo_));

    FullFrozenV["rspq"] = ThreeIntegral["grs"]*ThreeIntegral["gpq"];
    FullFrozenV["rspq"] -=ThreeIntegral["grq"]*ThreeIntegral["gps"];
    FullFrozenVAB["rspq"] = ThreeIntegral["grs"]*ThreeIntegral["gpq"];


    //Fill the SharedMatrix as frozen^2 by nmo^2
    //mo_to_rel[i[0]] gives the relative index in the first row ie orbital 28th which is frozen is the 3rd frozen orbital
    FullFrozenV.citerate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,const double& value){
        FrozenVMatrix->set(mo_to_rel[i[0]] * frozen_size + mo_to_rel[i[1]], i[2] * nmo_ + i[3], value);
    });
    FullFrozenVAB.citerate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,const double& value){
        FrozenVMatrixAB->set(mo_to_rel[i[0]] * frozen_size + mo_to_rel[i[1]], i[2] * nmo_ + i[3], value);
    });
    f = 0;

    //Tried to do this with ambit, but it seems that this is not really a contraction
    //Ambit complains and so I revert back to origin way, but I contract out the auxiliary index
    //This could become a problem for large systems
    for (size_t hi = 0; hi < nirrep_; ++hi){
        for (size_t i = 0; i < frzcpi_[hi]; ++i){
            size_t r = f + i;
            outfile->Printf("\n  Freezing MO %lu",r);
            #pragma omp parallel for num_threads(num_threads_) \
            schedule(dynamic)
            for(size_t p = 0; p < nmo_; ++p){
                for(size_t q = 0; q < nmo_; ++q){
                    one_electron_integrals_a[p * nmo_ + q] += FrozenVMatrix->get(mo_to_rel[r] * frozen_size + mo_to_rel[r], p * nmo_ + q)
                            + FrozenVMatrixAB->get(mo_to_rel[r] * frozen_size + mo_to_rel[r], p * nmo_ + q);
                    one_electron_integrals_b[p * nmo_ + q] += FrozenVMatrix->get(mo_to_rel[r] * frozen_size +mo_to_rel[r], p * nmo_ + q)
                            + FrozenVMatrixAB->get(mo_to_rel[r] * frozen_size + mo_to_rel[r], p * nmo_ + q);
                }
            }
        }
        f += nmopi_[hi];
    }

    ambit::BlockedTensor::reset_mo_spaces();
    outfile->Printf("\n\n FrozenOneBody Operator takes  %8.8f s", FrozenOneBody.get());
}

}}