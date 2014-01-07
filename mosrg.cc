#include <cmath>

#include <boost/numeric/odeint.hpp>

#include <libpsio/psio.hpp>
#include <libmints/wavefunction.h>
#include <libmints/molecule.h>

#include "mosrg.h"

using namespace std;
using namespace psi;
using namespace boost::numeric::odeint;
int nstepps = 0;

namespace psi{ namespace libadaptive{

MOSRG::MOSRG(Options &options, ExplorerIntegrals* ints, TwoIndex G1aa, TwoIndex G1bb)
    : MOBase(options,ints,G1aa,G1bb), srgop(SRGOpUnitary), srgcomm(SRCommutators)
{
    fprintf(outfile,"\n\n      --------------------------------------");
    fprintf(outfile,"\n          Similarity Renormalization Group");
    fprintf(outfile,"\n                Spin-integrated code");
    fprintf(outfile,"\n");
    fprintf(outfile,"\n                Version 0.1.0");
    fprintf(outfile,"\n");
    fprintf(outfile,"\n       written by Francesco A. Evangelista");
    fprintf(outfile,"\n      --------------------------------------\n");
    fflush(outfile);
    mosrg_startup();
    if(options_.get_str("SRG_MODE") == "SRG"){
        compute_similarity_renormalization_group();
    }else if(options_.get_str("SRG_MODE") == "CT"){
        compute_canonical_transformation_energy();
    }else if(options_.get_str("SRG_MODE") == "DSRG"){
        compute_driven_srg_energy();
    }
    print_timings();
}

MOSRG::~MOSRG()
{
}


/* The rhs of x' = f(x) */
void MOSRG_ODEInterface::operator() (const odeint_state_type& x,odeint_state_type& dxdt,const double t)
{
//    fprintf(outfile,"\n  Computing the Hamiltonian flow at time %f",t);

    // Step 1. Read the Hamiltonian from the vector x
    int nmo_ = mosrg_obj_.nmo_;
    mosrg_obj_.Hbar0_ = x[0];
    int i = 1;
    loop_mo_p loop_mo_q{
        mosrg_obj_.Hbar1_.aa[p][q] = x[i];
        i += 1;
    }
    loop_mo_p loop_mo_q{
        mosrg_obj_.Hbar1_.bb[p][q] = x[i];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        mosrg_obj_.Hbar2_.aaaa[p][q][r][s] = x[i];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        mosrg_obj_.Hbar2_.abab[p][q][r][s] = x[i];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        mosrg_obj_.Hbar2_.bbbb[p][q][r][s] = x[i];
        i += 1;
    }

    // Step 2. Compute the SRG Hamiltonian flow from Hbar and store it in S
    mosrg_obj_.compute_similarity_renormalization_group_step();


    // Step 3. Store the Hamiltonian flow in the rhs of the ODE
    dxdt[0] = mosrg_obj_.S0_;
    i = 1;
    loop_mo_p loop_mo_q{
        dxdt[i] = mosrg_obj_.S1_.aa[p][q];
        i += 1;
    }
    loop_mo_p loop_mo_q{
        dxdt[i] = mosrg_obj_.S1_.bb[p][q];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        dxdt[i] = mosrg_obj_.S2_.aaaa[p][q][r][s];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        dxdt[i] = mosrg_obj_.S2_.abab[p][q][r][s];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        dxdt[i] = mosrg_obj_.S2_.bbbb[p][q][r][s];
        i += 1;
    }
    neval_ += 1;
}

struct push_back_state_and_time
{
    std::vector< double >& m_states;
    std::vector< double >& m_times;

    push_back_state_and_time(std::vector<double> &states,std::vector<double> &times)
    : m_states(states), m_times(times) {}

    void operator()(const odeint_state_type& x,double t)
    {
        m_states.push_back( x[0] );
        m_times.push_back( t );
        fprintf(outfile,"\n %9d %20.12f %20.12f",int(m_states.size()),t,x[0]);
    }
};

void MOSRG::compute_similarity_renormalization_group()
{
    vector<double> e_vec,times;

    odeint_state_type x(1 + 2 * nmo_ * nmo_ + 3 * nmo_ * nmo_ * nmo_ * nmo_);
    // Initialize Hbar with the normal ordered Hamiltonian
    Hbar0_ = E0_;
    x[0] = E0_;
    int i = 1;
    loop_mo_p loop_mo_q{
        x[i] = Hbar1_.aa[p][q] = F_.aa[p][q];
        i += 1;
    }
    loop_mo_p loop_mo_q{
        x[i] = Hbar1_.bb[p][q] = F_.bb[p][q];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        x[i] = Hbar2_.aaaa[p][q][r][s] = V_.aaaa[p][q][r][s];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        x[i] = Hbar2_.abab[p][q][r][s] = V_.abab[p][q][r][s];
        i += 1;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        x[i] = Hbar2_.bbbb[p][q][r][s] = V_.bbbb[p][q][r][s];
        i += 1;
    }

    MOSRG_ODEInterface mosrg_flow_computer(*this);

//    size_t steps = integrate(sosrg_flow_computer,
//            x , 0.0 , 35.0 , 0.001 ,
//            push_back_state_and_time( e_vec , times ) );

//    size_t steps = integrate_adaptive(make_dense_output( 1.0e-12 , 1.0e-12 , runge_kutta_dopri5< state_type >() ),
//                       sosrg_flow_computer,
//                       x,0.0,10.0,0.001,
//                       push_back_state_and_time( e_vec , times ));
    double start_time = 0.0;
    double end_time = options_.get_double("SRG_SMAX");
    double initial_step = options_.get_double("SRG_DT");
    string srg_odeint = options_.get_str("SRG_ODEINT");
    double absolute_error_tollerance = options_.get_double("SRG_ODEINT_ABSERR");
    double relative_error_tollerance = options_.get_double("SRG_ODEINT_RELERR");

    fprintf(outfile,"\n  Start time:        %f",start_time);
    fprintf(outfile,"\n  End time:          %f",end_time);
    fprintf(outfile,"\n  Initial time step: %f",initial_step);

    size_t steps = 0;
    if (srg_odeint == "FEHLBERG78"){
        fprintf(outfile,"\n\n  Integrating the SRG equations using the Fehlberg 78 algorithm");
        integrate_adaptive(
                    make_controlled(absolute_error_tollerance,
                                    relative_error_tollerance,
                                    runge_kutta_fehlberg78<odeint_state_type>()),
                    mosrg_flow_computer,
                    x,start_time,end_time,initial_step,
                    push_back_state_and_time( e_vec , times ));
    }else if (srg_odeint == "CASHKARP"){
        fprintf(outfile,"\n\n  Integrating the SRG equations using the Cash-Karp 54 algorithm");
        integrate_adaptive(
                    make_controlled(absolute_error_tollerance,
                                    relative_error_tollerance,
                                    runge_kutta_cash_karp54<odeint_state_type>()),
                    mosrg_flow_computer,
                    x,start_time,end_time,initial_step,
                    push_back_state_and_time( e_vec , times ));
    }else if (srg_odeint == "DOPRI5"){
        fprintf(outfile,"\n\n  Integrating the SRG equations using the Dormand-Prince 5 algorithm");
        integrate_adaptive(
                    make_controlled(absolute_error_tollerance,
                                    relative_error_tollerance,
                                    runge_kutta_dopri5<odeint_state_type>()),
                    mosrg_flow_computer,
                    x,start_time,end_time,initial_step,
                    push_back_state_and_time( e_vec , times ));
    }
//    fprintf(outfile,"\n  Total steps: %d",int(steps));
//    for( size_t i=0; i<=steps; i++ )
//    {
//        fprintf(outfile,"\n %20.12f %20.12f",times[i],e_vec[i]);
//    }
    fprintf(outfile,"\n\n  The SRG integration required %d evaluations",nstepps);
}

void MOSRG::compute_similarity_renormalization_group_step()
{
    nstepps++;
//    fprintf(outfile,"\n  |Hbar1| = %20e |Hbar2| = %20e",norm(Hbar1_),norm(Hbar2_));


    // Step 1. Compute the generator (stored in eta)
    if (options_.get_str("SRG_ETA") == "WEGNER_BLOCK"){
        // a. copy the Hamiltonian
        add(1.0,Hbar1_,0.0,O1_);
        add(1.0,Hbar2_,0.0,O2_);

        // b. zero the off-diagonal blocks of G
        loop_mo_p loop_mo_q{
            if (Nv_.a[p] * No_.a[q] == 1.0){
                O1_.aa[p][q] = 0.0;
                O1_.aa[q][p] = 0.0;
            }
        }
        loop_mo_p loop_mo_q{
            if (Nv_.b[p] * No_.b[q] == 1.0){
                O1_.bb[p][q] = 0.0;
                O1_.bb[q][p] = 0.0;
            }
        }
        loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
            if (Nv_.a[p] * Nv_.a[q] * No_.a[r] * No_.a[s] == 1.0){
                O2_.aaaa[p][q][r][s] = 0.0;
                O2_.aaaa[r][s][p][q] = 0.0;
            }
        }
        loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
            if (Nv_.a[p] * Nv_.b[q] * No_.a[r] * No_.b[s] == 1.0){
                O2_.abab[p][q][r][s] = 0.0;
                O2_.abab[r][s][p][q] = 0.0;
            }
        }
        loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
            if (Nv_.b[p] * Nv_.b[q] * No_.b[r] * No_.b[s] == 1.0){
                O2_.bbbb[p][q][r][s] = 0.0;
                O2_.bbbb[r][s][p][q] = 0.0;
            }
        }

        // c. compute the generator as eta = [G,H]
        double eta0 = 0.0;
        zero(eta1_);
        zero(eta2_);
        commutator_A_B_C(1.0,O1_,O2_,Hbar1_,Hbar2_,eta0,eta1_,eta2_);

    }else if (options_.get_str("SRG_ETA") == "WEGNER_DIAG"){
        // a. Copy the diagonal of Hbar1 to G
        zero(O1_);
        zero(O2_);        
        loop_mo_p{
            O1_.aa[p][p] = Hbar1_.aa[p][p];
        }
        loop_mo_p{
            O1_.bb[p][p] = Hbar1_.bb[p][p];
        }

        // b. compute the generator as eta = [G,H]
        double eta0 = 0.0;
        zero(eta1_);
        zero(eta2_);
        commutator_A_B_C(1.0,O1_,O2_,Hbar1_,Hbar2_,eta0,eta1_,eta2_);

    }else if (options_.get_str("SRG_ETA") == "WHITE"){
        loop_mo_p loop_mo_q{
            if (Nv_.a[p] * No_.a[q] == 1.0){
                double e1 = Hbar1_.aa[p][q] / (Hbar1_.aa[p][p] - Hbar1_.aa[q][q] - Hbar2_.aaaa[p][q][p][q]);
                eta1_.aa[p][q] = e1;
                eta1_.aa[q][p] = -e1;
            }
        }
        loop_mo_p loop_mo_q{
            if (Nv_.b[p] * No_.b[q] == 1.0){
                double e1 = Hbar1_.bb[p][q] / (Hbar1_.bb[p][p] - Hbar1_.bb[q][q] - Hbar2_.bbbb[p][q][p][q]);
                eta1_.bb[p][q] = e1;
                eta1_.bb[q][p] = -e1;
            }
        }
        loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
            if (Nv_.a[p] * Nv_.a[q] * No_.a[r] * No_.a[s] == 1.0){
                double A = + Hbar2_.aaaa[p][q][p][q] + Hbar2_.aaaa[r][s][r][s]
                           - Hbar2_.aaaa[p][r][p][r] - Hbar2_.aaaa[q][s][q][s]
                           - Hbar2_.aaaa[p][s][p][s] - Hbar2_.aaaa[q][r][q][r];
                double e2 = Hbar2_.aaaa[p][q][r][s] / (Hbar1_.aa[p][p] + Hbar1_.aa[q][q] - Hbar1_.aa[r][r] - Hbar1_.aa[s][s] + A);
                eta2_.aaaa[p][q][r][s] = +e2;
                eta2_.aaaa[r][s][p][q] = -e2;
            }
        }
        loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
            if (Nv_.a[p] * Nv_.b[q] * No_.a[r] * No_.b[s] == 1.0){
                double A = + Hbar2_.abab[p][q][p][q] + Hbar2_.abab[r][s][r][s]
                           - Hbar2_.aaaa[p][r][p][r] - Hbar2_.bbbb[q][s][q][s]
                           - Hbar2_.abab[p][s][p][s] - Hbar2_.abab[r][q][r][q];
                double e2 = Hbar2_.abab[p][q][r][s] / (Hbar1_.aa[p][p] + Hbar1_.bb[q][q] - Hbar1_.aa[r][r] - Hbar1_.bb[s][s] + A);
                eta2_.abab[p][q][r][s] = +e2;
                eta2_.abab[r][s][p][q] = -e2;
            }
        }
        loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
            if (Nv_.b[p] * Nv_.b[q] * No_.b[r] * No_.b[s] == 1.0){
                double A = + Hbar2_.bbbb[p][q][p][q] + Hbar2_.bbbb[r][s][r][s]
                           - Hbar2_.bbbb[p][r][p][r] - Hbar2_.bbbb[q][s][q][s]
                           - Hbar2_.bbbb[p][s][p][s] - Hbar2_.bbbb[q][r][q][r];
                double e2 = Hbar2_.bbbb[p][q][r][s] / (Hbar1_.bb[p][p] + Hbar1_.bb[q][q] - Hbar1_.bb[r][r] - Hbar1_.bb[s][s] + A);
                eta2_.bbbb[p][q][r][s] = +e2;
                eta2_.bbbb[r][s][p][q] = -e2;
            }
        }
    }

    // Step 2. Compute the Hamiltonian flow as dH/ds = [eta,H] (stored in S)
    S0_ = 0.0;
    zero(S1_);
    zero(S2_);
    if (options_.get_str("SRG_COMM") == "STANDARD"){
        commutator_A_B_C(1.0,eta1_,eta2_,Hbar1_,Hbar2_,S0_,S1_,S2_);
    }else if (options_.get_str("SRG_COMM") == "FO"){
        commutator_A_B_C_fourth_order(1.0,eta1_,eta2_,Hbar1_,Hbar2_,S0_,S1_,S2_);
    }else  if (options_.get_str("SRG_COMM") == "SRG2"){
        commutator_A_B_C_SRG2(1.0,eta1_,eta2_,Hbar1_,Hbar2_,S0_,S1_,S2_);
    }
//    commutator_A_B_C(1.0,eta1_,eta2_,Hbar1_,Hbar2_,S0_,S1_,S2_);
}

void MOSRG::compute_canonical_transformation_energy()
{
    fprintf(outfile,"\n\n  ######################################");
    fprintf(outfile,"\n  ### Computing the CCSD BCH energy  ###");
    fprintf(outfile,"\n  ######################################");
    // Start the CCSD cycle
    double old_energy = 0.0;
    bool   converged  = false;
    int    cycle      = 0;
    compute_recursive_single_commutator();
    while(!converged){
        fprintf(outfile,"\n  Updating the S amplitudes...");
        fflush(outfile);
        update_S1();
        update_S2();
        fprintf(outfile," done.");
        fflush(outfile);

        fprintf(outfile,"\n  Compute recursive single commutator...");
        fflush(outfile);
        double energy = compute_recursive_single_commutator();
        fprintf(outfile," done.");
        fflush(outfile);


        fprintf(outfile,"\n  --------------------------------------------");
        fprintf(outfile,"\n  nExc           |S|                  |R|");
        fprintf(outfile,"\n  --------------------------------------------");
        fprintf(outfile,"\n    1     %15e      %15e",norm(S1_),norm(S1_));
        fprintf(outfile,"\n    2     %15e      %15e",norm(S2_),norm(S2_));
        fprintf(outfile,"\n  --------------------------------------------");
        double delta_energy = energy-old_energy;
        old_energy = energy;
        fprintf(outfile,"\n  @CC %4d %25.15f %25.15f",cycle,energy,delta_energy);

        if(fabs(delta_energy) < options_.get_double("E_CONVERGENCE")){
            converged = true;
        }

        if(cycle > options_.get_int("MAXITER")){
            fprintf(outfile,"\n\n\tThe calculation did not converge in %d cycles\n\tQuitting PSIMRCC\n",options_.get_int("MAXITER"));
            fflush(outfile);
//            exit(1);
            converged = true;
//            break;
        }
        fflush(outfile);
        cycle++;

        fprintf(outfile,"\n  NEXT CYCLE");
        fflush(outfile);
    }
    fprintf(outfile,"\n\n      * CCSD-BCH total energy      = %25.15f",old_energy);
}

void MOSRG::compute_driven_srg_energy()
{
    fprintf(outfile,"\n\n  ########################################");
    fprintf(outfile,"\n  ###  Computing the Driven SRG Energy ###");
    fprintf(outfile,"\n  ########################################");
    // Start the CCSD cycle
    double old_energy = 0.0;
    bool   converged  = false;
    int    cycle      = 0;
    compute_recursive_single_commutator();
    one_body_driven_srg();
    two_body_driven_srg();

    while(!converged){
        fprintf(outfile,"\n  Updating the S amplitudes...");
        fflush(outfile);
        update_S1();
        update_S2();
        fprintf(outfile," done.");
        fflush(outfile);

        fprintf(outfile,"\n  Compute recursive single commutator...");
        fflush(outfile);
        double energy = compute_recursive_single_commutator();
        one_body_driven_srg();
        two_body_driven_srg();
        fprintf(outfile," done.");
        fflush(outfile);


        fprintf(outfile,"\n  --------------------------------------------");
        fprintf(outfile,"\n  nExc           |S|                  |R|");
        fprintf(outfile,"\n  --------------------------------------------");
        fprintf(outfile,"\n    1     %15e      %15e",norm(S1_),norm(S1_));
        fprintf(outfile,"\n    2     %15e      %15e",norm(S2_),norm(S2_));
        fprintf(outfile,"\n  --------------------------------------------");
        double delta_energy = energy-old_energy;
        old_energy = energy;
        fprintf(outfile,"\n  @CC %4d %25.15f %25.15f",cycle,energy,delta_energy);

        if(fabs(delta_energy) < options_.get_double("E_CONVERGENCE")){
            converged = true;
        }

        if(cycle > options_.get_int("MAXITER")){
            fprintf(outfile,"\n\n\tThe calculation did not converge in %d cycles\n\tQuitting PSIMRCC\n",options_.get_int("MAXITER"));
            fflush(outfile);
//            exit(1);
            converged = true;
//            break;
        }
        fflush(outfile);
        cycle++;

        fprintf(outfile,"\n  NEXT CYCLE");
        fflush(outfile);
    }
    fprintf(outfile,"\n\n      * DSRG(2) total energy      = %25.15f",old_energy);
}

double MOSRG::compute_recursive_single_commutator()
{
    fprintf(outfile,"\n\n  Computing the BCH expansion using the");
    if (srgcomm == SRCommutators){
        fprintf(outfile," single-reference normal ordering formalism.");
    }
    fprintf(outfile,"\n  -----------------------------------------------------------------");
    fprintf(outfile,"\n  nComm          |C1|                 |C2|                  E" );
    fprintf(outfile,"\n  -----------------------------------------------------------------");

    // Initialize Hbar and O with the Hamiltonian
    add(1.0,F_,0.0,Hbar1_);
    add(1.0,F_,0.0,O1_);
    add(1.0,V_,0.0,Hbar2_);
    add(1.0,V_,0.0,O2_);
    double E0 = E0_;

    fprintf(outfile,"\n  %2d %20e %20e %20.12f",0,norm(Hbar1_),norm(Hbar2_),E0);
    for (int n = 1; n < 20; ++n) {
        double factor = 1.0 / static_cast<double>(n);

        zero(C1_);
        zero(C2_);

        if (options_.get_str("SRG_COMM") == "STANDARD"){
            commutator_A_B_C(factor,O1_,O2_,S1_,S2_,E0,C1_,C2_);
        }else if (options_.get_str("SRG_COMM") == "FO"){
            commutator_A_B_C_fourth_order(factor,O1_,O2_,S1_,S2_,E0,C1_,C2_);
        }
        add(1.0,C1_,1.0,Hbar1_);
        add(1.0,C2_,1.0,Hbar2_);
        add(1.0,C1_,0.0,O1_);
        add(1.0,C2_,0.0,O2_);

        double norm_O2 = norm(O1_);
        double norm_O1 = norm(O2_);
        fprintf(outfile,"\n  %2d %20e %20e %20.12f",n,norm_O1,norm_O2,E0);
        if (std::sqrt(norm_O2 * norm_O2 + norm_O1 * norm_O1) < 1.0e-12){
            break;
        }
    }
    fprintf(outfile,"\n  -----------------------------------------------------------------");
    fflush(outfile);
    return E0;
}

void MOSRG::mosrg_startup()
{
    // Compute the MP2 energy
    double emp2 = 0.0;
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s {
        double numerator_aaaa = std::pow(V_.aaaa[p][q][r][s],2.0) * No_.a[p] * No_.a[q] * Nv_.a[r] * Nv_.a[s];
        double denominator_aaaa = F_.aa[p][p] + F_.aa[q][q] - F_.aa[r][r] - F_.aa[s][s];
        if (denominator_aaaa != 0.0)
            emp2 += 0.25 * numerator_aaaa / denominator_aaaa;

        double numerator_bbbb = std::pow(V_.bbbb[p][q][r][s],2.0) * No_.b[p] * No_.b[q] * Nv_.b[r] * Nv_.b[s];
        double denominator_bbbb = F_.bb[p][p] + F_.bb[q][q] - F_.bb[r][r] - F_.bb[s][s];
        if (denominator_bbbb != 0.0)
            emp2 += 0.25 * numerator_bbbb / denominator_bbbb;

        double numerator_abab = std::pow(V_.abab[p][q][r][s],2.0) * No_.a[p] * No_.b[q] * Nv_.a[r] * Nv_.b[s];
        double denominator_abab = F_.aa[p][p] + F_.bb[q][q] - F_.aa[r][r] - F_.bb[s][s];
        if (denominator_abab != 0.0)
            emp2 += numerator_abab / denominator_abab;
    }


    fprintf(outfile,"\n\n  emp2 = %20.12f",emp2);


    if (options_.get_str("SRG_OP") == "UNITARY"){
        srgop = SRGOpUnitary;
        fprintf(outfile,"\n\n  Using a unitary operator\n");
    }
    if (options_.get_str("SRG_OP") == "CC"){
        srgop = SRGOpCC;
        fprintf(outfile,"\n\n  Using an excitation operator\n");
    }

    allocate(Hbar1_);
    allocate(Hbar2_);
    allocate(eta1_);
    allocate(eta2_);
    allocate(O1_);
    allocate(O2_);
    allocate(S1_);
    allocate(S2_);
    allocate(C1_);
    allocate(C2_);

    std::vector<size_t> n2 = {nmo_,nmo_};
    std::vector<size_t> n4 = {nmo_,nmo_,nmo_,nmo_};
    tA_aaaa.resize("A",n4);
    tA_abab.resize("A",n4);
    tA_bbbb.resize("A",n4);

    tB_aaaa.resize("B",n4);
    tB_abab.resize("B",n4);
    tB_bbbb.resize("B",n4);

    tAm_aaaa.resize("Amod",n4);
    tAm_abab.resize("Amod",n4);
    tAm_bbbb.resize("Amod",n4);

    tBm_aaaa.resize("Bmod",n4);
    tBm_abab.resize("Bmod",n4);
    tBm_bbbb.resize("Bmod",n4);

    tC_aaaa.resize("C",n4);
    tC_abab.resize("C",n4);
    tC_bbbb.resize("C",n4);
}

void MOSRG::mosrg_cleanup()
{
    release(Hbar1_);
    release(Hbar2_);
    release(eta1_);
    release(eta2_);
    release(O1_);
    release(O2_);
    release(S1_);
    release(S2_);
    release(C1_);
    release(C2_);
}

void MOSRG::update_S1()
{
    loop_mo_p loop_mo_q{
        if (F_.aa[p][p] - F_.aa[q][q] != 0.0){
            S1_.aa[p][q] += - Nv_.a[p] * No_.a[q] * Hbar1_.aa[p][q] / (F_.aa[p][p] - F_.aa[q][q]);
        }
        if (F_.bb[p][p] - F_.bb[q][q] != 0.0){
            S1_.bb[p][q] += - Nv_.b[p] * No_.b[q] * Hbar1_.bb[p][q] / (F_.bb[p][p] - F_.bb[q][q]);
        }
    }
    if (srgop == SRGOpUnitary){
        loop_mo_p loop_mo_q{
            if (F_.aa[p][p] - F_.aa[q][q] != 0.0){
                S1_.aa[p][q] += - No_.a[p] * Nv_.a[q] * Hbar1_.aa[p][q] / (F_.aa[p][p] - F_.aa[q][q]);
            }
            if (F_.bb[p][p] - F_.bb[q][q] != 0.0){
                S1_.bb[p][q] += - No_.b[p] * Nv_.b[q] * Hbar1_.bb[p][q] / (F_.bb[p][p] - F_.bb[q][q]);
            }
        }
    }
}

void MOSRG::update_S2()
{
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        if (F_.aa[p][p] + F_.aa[q][q] - F_.aa[r][r] - F_.aa[s][s] != 0.0){
            S2_.aaaa[p][q][r][s] += - Nv_.a[p] * Nv_.a[q] * No_.a[r] * No_.a[s] * Hbar2_.aaaa[p][q][r][s] / (F_.aa[p][p] + F_.aa[q][q] - F_.aa[r][r] - F_.aa[s][s]);
        }
        if (F_.aa[p][p] + F_.bb[q][q] - F_.aa[r][r] - F_.bb[s][s] != 0.0){
            S2_.abab[p][q][r][s] += - Nv_.a[p] * Nv_.b[q] * No_.a[r] * No_.b[s] * Hbar2_.abab[p][q][r][s] / (F_.aa[p][p] + F_.bb[q][q] - F_.aa[r][r] - F_.bb[s][s]);
        }
        if (F_.bb[p][p] + F_.bb[q][q] - F_.bb[r][r] - F_.bb[s][s] != 0.0){
            S2_.bbbb[p][q][r][s] += - Nv_.b[p] * Nv_.b[q] * No_.b[r] * No_.b[s] * Hbar2_.bbbb[p][q][r][s] / (F_.bb[p][p] + F_.bb[q][q] - F_.bb[r][r] - F_.bb[s][s]);
        }
    }

    if (srgop == SRGOpUnitary){
        loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
            if (F_.aa[p][p] + F_.aa[q][q] - F_.aa[r][r] - F_.aa[s][s] != 0.0){
                S2_.aaaa[p][q][r][s] += - No_.a[p] * No_.a[q] * Nv_.a[r] * Nv_.a[s] * Hbar2_.aaaa[p][q][r][s] / (F_.aa[p][p] + F_.aa[q][q] - F_.aa[r][r] - F_.aa[s][s]);
            }
            if (F_.aa[p][p] + F_.bb[q][q] - F_.aa[r][r] - F_.bb[s][s] != 0.0){
                S2_.abab[p][q][r][s] += - No_.a[p] * No_.b[q] * Nv_.a[r] * Nv_.b[s] * Hbar2_.abab[p][q][r][s] / (F_.aa[p][p] + F_.bb[q][q] - F_.aa[r][r] - F_.bb[s][s]);
            }
            if (F_.bb[p][p] + F_.bb[q][q] - F_.bb[r][r] - F_.bb[s][s] != 0.0){
                S2_.bbbb[p][q][r][s] += - No_.b[p] * No_.b[q] * Nv_.b[r] * Nv_.b[s] * Hbar2_.bbbb[p][q][r][s] / (F_.bb[p][p] + F_.bb[q][q] - F_.bb[r][r] - F_.bb[s][s]);
            }
        }
    }
}

void MOSRG::one_body_driven_srg()
{
    double end_time = options_.get_double("SRG_SMAX");
    loop_mo_p loop_mo_q{
        double factor = std::exp(- end_time * std::pow(F_.aa[p][p] - F_.aa[q][q],2.0));
        Hbar1_.aa[p][q] -= Nv_.a[p] * No_.a[q] * F_.aa[p][q] * factor;
        Hbar1_.aa[p][q] -= No_.a[p] * Nv_.a[q] * F_.aa[p][q] * factor;
    }
    loop_mo_p loop_mo_q{
        double factor = std::exp(- end_time * std::pow(F_.bb[p][p] - F_.bb[q][q],2.0));
        Hbar1_.bb[p][q] -= Nv_.b[p] * No_.b[q] * F_.bb[p][q] * factor;
        Hbar1_.bb[p][q] -= No_.b[p] * Nv_.b[q] * F_.bb[p][q] * factor;
    }
}

void MOSRG::two_body_driven_srg()
{
    double end_time = options_.get_double("SRG_SMAX");
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        double factor = std::exp(- end_time * std::pow(F_.aa[p][p] + F_.aa[q][q] - F_.aa[r][r] - F_.aa[s][s],2.0));
        Hbar2_.aaaa[p][q][r][s] -=  Nv_.a[p] * Nv_.a[q] * No_.a[r] * No_.a[s] * V_.aaaa[p][q][r][s] * factor;
        Hbar2_.aaaa[p][q][r][s] -=  No_.a[p] * No_.a[q] * Nv_.a[r] * Nv_.a[s] * V_.aaaa[p][q][r][s] * factor;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        double factor = std::exp(- end_time * std::pow(F_.aa[p][p] + F_.bb[q][q] - F_.aa[r][r] - F_.bb[s][s],2.0));
        Hbar2_.abab[p][q][r][s] -=  Nv_.a[p] * Nv_.b[q] * No_.a[r] * No_.b[s] * V_.abab[p][q][r][s] * factor;
        Hbar2_.abab[p][q][r][s] -=  No_.a[p] * No_.b[q] * Nv_.a[r] * Nv_.b[s] * V_.abab[p][q][r][s] * factor;
    }
    loop_mo_p loop_mo_q loop_mo_r loop_mo_s{
        double factor = std::exp(- end_time * std::pow(F_.bb[p][p] + F_.bb[q][q] - F_.bb[r][r] - F_.bb[s][s],2.0));
        Hbar2_.bbbb[p][q][r][s] -=  Nv_.b[p] * Nv_.b[q] * No_.b[r] * No_.b[s] * V_.bbbb[p][q][r][s] * factor;
        Hbar2_.bbbb[p][q][r][s] -=  No_.b[p] * No_.b[q] * Nv_.b[r] * Nv_.b[s] * V_.bbbb[p][q][r][s] * factor;
    }
}

}} // EndNamespaces