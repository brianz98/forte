#include <algorithm>
#include <vector>
#include <map>
#include <boost/format.hpp>
#include <ambit/tensor.h>

#include "helpers.h"
#include "mrdsrg.h"

namespace psi{ namespace forte{

double MRDSRG::compute_energy_pt2(){
    // print title
    outfile->Printf("\n\n  ==> Second-Order Perturbation DSRG-MRPT2 <==\n");
    outfile->Printf("\n    Reference:");
    outfile->Printf("\n      J. Chem. Theory Comput. 2015, 11, 2097-2108.\n");

    // create zeroth-order Hamiltonian
    H0th = BTF->build(tensor_type_,"Zeroth-order H",spin_cases({"gg"}));
    H0th.iterate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,double& value){
        if(i[0] == i[1]){
            if(spin[0] == AlphaSpin){
                value = Fa[i[0]];
            }else{
                value = Fb[i[0]];
            }
        }
    });

    // test orbitals are semi-canonicalized
    check_semicanonical();

    // initiate Hbar with bare Hamiltonian
    Hbar1 = BTF->build(tensor_type_,"Hbar1",spin_cases({"gg"}));
    Hbar2 = BTF->build(tensor_type_,"Hbar2",spin_cases({"gggg"}));
    Hbar1["pq"] = F["pq"];
    Hbar1["PQ"] = F["PQ"];
    Hbar2["pqrs"] = V["pqrs"];
    Hbar2["pQrS"] = V["pQrS"];
    Hbar2["PQRS"] = V["PQRS"];

    // compute H0th contribution to H1 and H2
    O1 = BTF->build(tensor_type_,"temp1",spin_cases({"gg"}));
    O2 = BTF->build(tensor_type_,"temp2",spin_cases({"gggg"}));
    H1_T1_C1(H0th,T1,0.5,O1);
    H1_T2_C1(H0th,T2,0.5,O1);
    H1_T2_C2(H0th,T2,0.5,O2);

    // [H, A] = [H, T] + [H, T]^dagger
    Hbar1["pq"] += O1["pq"];
    Hbar1["pq"] += O1["qp"];
    Hbar1["PQ"] += O1["PQ"];
    Hbar1["PQ"] += O1["QP"];
    Hbar2["pqrs"] += O2["pqrs"];
    Hbar2["pqrs"] += O2["rspq"];
    Hbar2["pQrS"] += O2["pQrS"];
    Hbar2["pQrS"] += O2["rSpQ"];
    Hbar2["PQRS"] += O2["PQRS"];
    Hbar2["PQRS"] += O2["RSPQ"];

    // compute PT2 energy
    std::vector<std::pair<std::string,double>> energy;
    energy.push_back({"E0 (reference)", Eref});
    double Ecorr = 0.0, Etemp = 0.0;

    H1_T1_C0(Hbar1,T1,1.0,Ecorr);
    energy.push_back({"<[F, A1]>", 2 * (Ecorr - Etemp)});
    Etemp = Ecorr;

    H1_T2_C0(Hbar1,T2,1.0,Ecorr);
    energy.push_back({"<[F, A2]>", 2 * (Ecorr - Etemp)});
    Etemp = Ecorr;

    H2_T1_C0(Hbar2,T1,1.0,Ecorr);
    energy.push_back({"<[V, A1]>", 2 * (Ecorr - Etemp)});
    Etemp = Ecorr;

    H2_T2_C0(Hbar2,T2,1.0,Ecorr);
    energy.push_back({"<[V, A2]>", 2 * (Ecorr - Etemp)});
    Etemp = Ecorr;

    // <[H, A]> = 2 * <[H, T]>
    Ecorr *= 2.0;

    energy.push_back({"DSRG-MRPT2 correlation energy", Ecorr});
    energy.push_back({"DSRG-MRPT2 total energy", Eref + Ecorr});

    outfile->Printf("\n\n  ==> DSRG-MRPT2 Energy Summary <==\n");
    for (auto& str_dim : energy){
        outfile->Printf("\n    %-30s = %22.15f",str_dim.first.c_str(),str_dim.second);
    }

    // reference relaxation
    if(options_.get_str("RELAX_REF") != "NONE"){
        O1.zero();
        O2.zero();

        H1_T1_C1(Hbar1,T1,1.0,O1);
        H1_T2_C1(Hbar1,T2,1.0,O1);
        H2_T1_C1(Hbar2,T1,1.0,O1);
        H2_T2_C1(Hbar2,T2,1.0,O1);

        H1_T2_C2(Hbar1,T2,1.0,O2);
        H2_T1_C2(Hbar2,T1,1.0,O2);
        H2_T2_C2(Hbar2,T2,1.0,O2);

        Hbar1["pq"] += O1["pq"];
        Hbar1["pq"] += O1["qp"];
        Hbar1["PQ"] += O1["PQ"];
        Hbar1["PQ"] += O1["QP"];
        Hbar2["pqrs"] += O2["pqrs"];
        Hbar2["pqrs"] += O2["rspq"];
        Hbar2["pQrS"] += O2["pQrS"];
        Hbar2["pQrS"] += O2["rSpQ"];
        Hbar2["PQRS"] += O2["PQRS"];
        Hbar2["PQRS"] += O2["RSPQ"];
    }

    Hbar0 = Ecorr;
    return Ecorr;
}

double MRDSRG::compute_energy_pt3(){
    // print title
    outfile->Printf("\n\n  ==> Third-Order Perturbation DSRG-MRPT3 <==\n");
    outfile->Printf("\n    Reference:");
    outfile->Printf("\n      J. Chem. Phys. (in preparation)\n");
    std::vector<std::pair<std::string,double>> energy;
    energy.push_back({"E0 (reference)", Eref});

    // create zeroth-order Hamiltonian
    H0th = BTF->build(tensor_type_,"Zeroth-order H",spin_cases({"gg"}));
    H0th.iterate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,double& value){
        if(i[0] == i[1]){
            if(spin[0] == AlphaSpin){
                value = Fa[i[0]];
            }else{
                value = Fb[i[0]];
            }
        }
    });

    // test orbitals are semi-canonicalized
    check_semicanonical();

    // create first-order bare Hamiltonian
    BlockedTensor H1st_1 = BTF->build(tensor_type_,"H1st_1",spin_cases({"gg"}));
    BlockedTensor H1st_2 = BTF->build(tensor_type_,"H1st_2",spin_cases({"gggg"}));
    H1st_1["pq"] = F["pq"];
    H1st_1["PQ"] = F["PQ"];
    for(auto block: {"cc", "aa", "vv", "CC", "AA", "VV"}){
        H1st_1.block(block).zero();
    }
    H1st_2["pqrs"] = V["pqrs"];
    H1st_2["pQrS"] = V["pQrS"];
    H1st_2["PQRS"] = V["PQRS"];

    // compute [H0th, T1st]
    O1 = BTF->build(tensor_type_,"temp1",spin_cases({"gg"}));
    O2 = BTF->build(tensor_type_,"temp2",spin_cases({"gggg"}));
    H1_T1_C1(H0th,T1,1.0,O1);
    H1_T2_C1(H0th,T2,1.0,O1);
    H1_T2_C2(H0th,T2,1.0,O2);

    // compute H~1st = H1st + 0.5 * [H0th, A1st]
    Hbar1 = BTF->build(tensor_type_,"temp1",spin_cases({"gg"}));
    Hbar2 = BTF->build(tensor_type_,"temp2",spin_cases({"gggg"}));
    Hbar1["pq"] += 0.5 * O1["pq"];
    Hbar1["pq"] += 0.5 * O1["qp"];
    Hbar1["PQ"] += 0.5 * O1["PQ"];
    Hbar1["PQ"] += 0.5 * O1["QP"];
    Hbar2["pqrs"] += 0.5 * O2["pqrs"];
    Hbar2["pqrs"] += 0.5 * O2["rspq"];
    Hbar2["pQrS"] += 0.5 * O2["pQrS"];
    Hbar2["pQrS"] += 0.5 * O2["rSpQ"];
    Hbar2["PQRS"] += 0.5 * O2["PQRS"];
    Hbar2["PQRS"] += 0.5 * O2["RSPQ"];

    Hbar1["pq"] += H1st_1["pq"];
    Hbar1["PQ"] += H1st_1["PQ"];
    Hbar2["pqrs"] += H1st_2["pqrs"];
    Hbar2["pQrS"] += H1st_2["pQrS"];
    Hbar2["PQRS"] += H1st_2["PQRS"];

    // compute second-order energy
    double Ept2 = 0.0;
    H1_T1_C0(Hbar1,T1,1.0,Ept2);
    H1_T2_C0(Hbar1,T2,1.0,Ept2);
    H2_T1_C0(Hbar2,T1,1.0,Ept2);
    H2_T2_C0(Hbar2,T2,1.0,Ept2);
    Ept2 *= 2.0;
    energy.push_back({"2nd-order correlation energy", Ept2});

    // compute [H~1st, T1st]
    O1.zero();
    O2.zero();
    H1_T1_C1(Hbar1,T1,1.0,O1);
    H1_T2_C1(Hbar1,T2,1.0,O1);
    H2_T1_C1(Hbar2,T1,1.0,O1);
    H2_T2_C1(Hbar2,T2,1.0,O1);
    H1_T2_C2(Hbar1,T2,1.0,O2);
    H2_T1_C2(Hbar2,T1,1.0,O2);
    H2_T2_C2(Hbar2,T2,1.0,O2);

    // compute second-order Hbar
    BlockedTensor Hbar2nd_1 = BTF->build(tensor_type_,"Hbar2nd_1",spin_cases({"gg"}));
    BlockedTensor Hbar2nd_2 = BTF->build(tensor_type_,"Hbar2nd_2",spin_cases({"gggg"}));
    Hbar2nd_1["pq"] += O1["pq"];
    Hbar2nd_1["pq"] += O1["qp"];
    Hbar2nd_1["PQ"] += O1["PQ"];
    Hbar2nd_1["PQ"] += O1["QP"];
    Hbar2nd_2["pqrs"] += O2["pqrs"];
    Hbar2nd_2["pqrs"] += O2["rspq"];
    Hbar2nd_2["pQrS"] += O2["pQrS"];
    Hbar2nd_2["pQrS"] += O2["rSpQ"];
    Hbar2nd_2["PQRS"] += O2["PQRS"];
    Hbar2nd_2["PQRS"] += O2["RSPQ"];

    // compute second-order amplitudes
    BlockedTensor T2nd_1 = BTF->build(tensor_type_,"temp1",spin_cases({"hp"}));
    BlockedTensor T2nd_2 = BTF->build(tensor_type_,"temp2",spin_cases({"hhpp"}));
    guess_t2(Hbar2nd_2,T2nd_2);
    guess_t1(Hbar2nd_1,T2nd_2,T2nd_1);
    analyze_amplitudes("Second-Order",T2nd_1,T2nd_2);

    // compute <[H~1st, A2nd]>
    double Ecorr1 = 0.0;
    H1_T1_C0(Hbar1,T2nd_1,1.0,Ecorr1);
    H1_T2_C0(Hbar1,T2nd_2,1.0,Ecorr1);
    H2_T1_C0(Hbar2,T2nd_1,1.0,Ecorr1);
    H2_T2_C0(Hbar2,T2nd_2,1.0,Ecorr1);
    Ecorr1 *= 2.0;
    energy.push_back({"<[H~1st, A2nd]>", Ecorr1});

    // compute 1 / 3 * [H~1st, A1st]
    Hbar2nd_1.scale(1.0/3.0);
    Hbar2nd_2.scale(1.0/3.0);

    // compute 1 / 2 * [H0th, A2nd] + 1 / 6 * [H1st, A1st]
    O1.zero();
    O2.zero();
    H1_T1_C1(H0th,T2nd_1,0.5,O1);
    H1_T2_C1(H0th,T2nd_2,0.5,O1);
    H1_T2_C2(H0th,T2nd_2,0.5,O2);

    H1_T1_C1(H1st_1,T1,1.0/6.0,O1);
    H1_T2_C1(H1st_1,T2,1.0/6.0,O1);
    H2_T1_C1(H1st_2,T1,1.0/6.0,O1);
    H2_T2_C1(H1st_2,T2,1.0/6.0,O1);
    H1_T2_C2(H1st_1,T2,1.0/6.0,O2);
    H2_T1_C2(H1st_2,T1,1.0/6.0,O2);
    H2_T2_C2(H1st_2,T2,1.0/6.0,O2);

    Hbar2nd_1["pq"] += O1["pq"];
    Hbar2nd_1["pq"] += O1["qp"];
    Hbar2nd_1["PQ"] += O1["PQ"];
    Hbar2nd_1["PQ"] += O1["QP"];
    Hbar2nd_2["pqrs"] += O2["pqrs"];
    Hbar2nd_2["pqrs"] += O2["rspq"];
    Hbar2nd_2["pQrS"] += O2["pQrS"];
    Hbar2nd_2["pQrS"] += O2["rSpQ"];
    Hbar2nd_2["PQRS"] += O2["PQRS"];
    Hbar2nd_2["PQRS"] += O2["RSPQ"];

    // compute <[H~2nd, A1st]>
    double Ecorr2 = 0.0;
    H1_T1_C0(Hbar2nd_1,T1,1.0,Ecorr2);
    H1_T2_C0(Hbar2nd_1,T2,1.0,Ecorr2);
    H2_T1_C0(Hbar2nd_2,T1,1.0,Ecorr2);
    H2_T2_C0(Hbar2nd_2,T2,1.0,Ecorr2);
    Ecorr2 *= 2.0;
    energy.push_back({"<[H~2nd, A1st]>", Ecorr2});

    // print summary
    double Ecorr = Ecorr1 + Ecorr2;
    energy.push_back({"3rd-order correlation energy", Ecorr});
    Ecorr += Ept2;
    energy.push_back({"DSRG-MRPT3 correlation energy", Ecorr});
    energy.push_back({"DSRG-MRPT3 total energy", Eref + Ecorr});

    outfile->Printf("\n\n  ==> DSRG-MRPT3 Energy Summary <==\n");
    for (auto& str_dim : energy){
        outfile->Printf("\n    %-30s = %22.15f",str_dim.first.c_str(),str_dim.second);
    }

    if(options_.get_str("RELAX_REF") != "NONE"){
        O1.zero();
        O2.zero();

        // [H~1st, A1st]
        H1_T1_C1(Hbar1,T1,1.0,O1);
        H1_T2_C1(Hbar1,T2,1.0,O1);
        H2_T1_C1(Hbar2,T1,1.0,O1);
        H2_T2_C1(Hbar2,T2,1.0,O1);
        H1_T2_C2(Hbar1,T2,1.0,O2);
        H2_T1_C2(Hbar2,T1,1.0,O2);
        H2_T2_C2(Hbar2,T2,1.0,O2);

        // [H~2nd, A1st]
        H1_T1_C1(Hbar2nd_1,T1,1.0,O1);
        H1_T2_C1(Hbar2nd_1,T2,1.0,O1);
        H2_T1_C1(Hbar2nd_2,T1,1.0,O1);
        H2_T2_C1(Hbar2nd_2,T2,1.0,O1);
        H1_T2_C2(Hbar2nd_1,T2,1.0,O2);
        H2_T1_C2(Hbar2nd_2,T1,1.0,O2);
        H2_T2_C2(Hbar2nd_2,T2,1.0,O2);

        // [H~1st, A2nd]
        H1_T1_C1(Hbar1,T2nd_1,1.0,O1);
        H1_T2_C1(Hbar1,T2nd_2,1.0,O1);
        H2_T1_C1(Hbar2,T2nd_1,1.0,O1);
        H2_T2_C1(Hbar2,T2nd_2,1.0,O1);
        H1_T2_C2(Hbar1,T2nd_2,1.0,O2);
        H2_T1_C2(Hbar2,T2nd_1,1.0,O2);
        H2_T2_C2(Hbar2,T2nd_2,1.0,O2);

        Hbar1["pq"] += O1["pq"];
        Hbar1["pq"] += O1["qp"];
        Hbar1["PQ"] += O1["PQ"];
        Hbar1["PQ"] += O1["QP"];
        Hbar2["pqrs"] += O2["pqrs"];
        Hbar2["pqrs"] += O2["rspq"];
        Hbar2["pQrS"] += O2["pQrS"];
        Hbar2["pQrS"] += O2["rSpQ"];
        Hbar2["PQRS"] += O2["PQRS"];
        Hbar2["PQRS"] += O2["RSPQ"];

        Hbar1["pq"] += H0th["pq"];
        Hbar1["PQ"] += H0th["PQ"];
    }

    Hbar0 = Ecorr;
    return Ecorr;
}

void MRDSRG::check_semicanonical(){
    outfile->Printf("\n    Checking if orbitals are semi-canonicalized ...");
    std::vector<std::string> blocks{"cc","aa","vv","CC","AA","VV"};
    std::vector<double> Foff;
    double Foff_sum = 0.0;
    for(auto& block: blocks){
        double value = std::pow(F.block(block).norm(), 2.0) - std::pow(H0th.block(block).norm(), 2.0);
        value = std::sqrt(value);
        Foff.push_back(value);
        Foff_sum += value;
    }
    double threshold = 10.0 * options_.get_double("D_CONVERGENCE");
    if(Foff_sum > threshold){
        std::string sep(3 + 16 * 3, '-');
        outfile->Printf("\n    Warning! Orbitals are not semi-canonicalized!");
        outfile->Printf("\n    DSRG-MRPT2 is currently only formulated using semi-canonical orbitals!");
        outfile->Printf("\n    Off-Diagonal norms of the core, active, virtual blocks of Fock matrix");
        outfile->Printf("\n       %15s %15s %15s", "core", "active", "virtual");
        outfile->Printf("\n    %s", sep.c_str());
        outfile->Printf("\n    Fa %15.10f %15.10f %15.10f", Foff[0], Foff[1], Foff[2]);
        outfile->Printf("\n    Fb %15.10f %15.10f %15.10f", Foff[3], Foff[4], Foff[5]);
        outfile->Printf("\n    %s\n", sep.c_str());
        throw PSIEXCEPTION("Orbitals are not semi-canonicalized.");
    }else{
        outfile->Printf("     OK.");
    }
}

}}