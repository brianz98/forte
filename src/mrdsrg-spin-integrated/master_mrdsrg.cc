#include <numeric>

#include "psi4/libpsi4util/process.h"
#include "psi4/libmints/molecule.h"

#include "master_mrdsrg.h"

namespace psi {
namespace forte {

MASTER_DSRG::MASTER_DSRG(Reference reference, SharedWavefunction ref_wfn, Options& options,
                         std::shared_ptr<ForteIntegrals> ints,
                         std::shared_ptr<MOSpaceInfo> mo_space_info)
    : DynamicCorrelationSolver(reference, ref_wfn, options, ints, mo_space_info),
      tensor_type_(ambit::CoreTensor), BTF_(new BlockedTensorFactory(options)) {
    reference_wavefunction_ = ref_wfn;
    startup();
}

void MASTER_DSRG::startup() {
    // read options
    read_options();

    // read orbital spaces
    read_MOSpaceInfo();

    // set Ambit MO space labels
    set_ambit_MOSpace();

    // read commonly used energies
    Eref_ = reference_.get_Eref();
    Enuc_ = Process::environment.molecule()->nuclear_repulsion_energy();
    Efrzc_ = ints_->frozen_core_energy();

    // initialize timer for commutator
    dsrg_time_ = DSRG_TIME();

    // prepare density matrix and cumulants
    Eta1_ = BTF_->build(tensor_type_, "Eta1", spin_cases({"aa"}));
    Gamma1_ = BTF_->build(tensor_type_, "Gamma1", spin_cases({"aa"}));
    Lambda2_ = BTF_->build(tensor_type_, "Lambda2", spin_cases({"aaaa"}));
    fill_density();
}

void MASTER_DSRG::read_options() {
    auto throw_error = [&](const std::string& message) -> void {
        outfile->Printf("\n  %s", message.c_str());
        throw PSIEXCEPTION(message);
    };

    print_ = options_.get_int("PRINT");

    s_ = options_.get_double("DSRG_S");
    if (s_ < 0) {
        throw_error("S parameter for DSRG must >= 0!");
    }
    taylor_threshold_ = options_.get_int("TAYLOR_THRESHOLD");
    if (taylor_threshold_ <= 0) {
        throw_error("Threshold for Taylor expansion must be an integer greater than 0!");
    }

    source_ = options_.get_str("SOURCE");
    if (source_ != "STANDARD" && source_ != "LABS" && source_ != "DYSON") {
        outfile->Printf("\n  Warning: SOURCE option %s is not implemented.", source_.c_str());
        outfile->Printf("\n  Changed SOURCE option to STANDARD");
        source_ = "STANDARD";
    }
    if (source_ == "STANDARD") {
        dsrg_source_ = std::make_shared<STD_SOURCE>(s_, taylor_threshold_);
    } else if (source_ == "LABS") {
        dsrg_source_ = std::make_shared<LABS_SOURCE>(s_, taylor_threshold_);
    } else if (source_ == "DYSON") {
        dsrg_source_ = std::make_shared<DYSON_SOURCE>(s_, taylor_threshold_);
    }

    ntamp_ = options_.get_int("NTAMP");
    intruder_tamp_ = options_.get_double("INTRUDER_TAMP");

    relax_ref_ = options_.get_str("RELAX_REF");

    eri_df_ = false;
    ints_type_ = options_.get_str("INT_TYPE");
    if (ints_type_ == "CHOLESKY" || ints_type_ == "DF" || ints_type_ == "DISKDF") {
        eri_df_ = true;
    }

    multi_state_ = options_["AVG_STATE"].size() != 0;
    multi_state_algorithm_ = options_.get_str("DSRG_MULTI_STATE");
}

void MASTER_DSRG::read_MOSpaceInfo() {
    core_mos_ = mo_space_info_->get_corr_abs_mo("RESTRICTED_DOCC");
    actv_mos_ = mo_space_info_->get_corr_abs_mo("ACTIVE");
    virt_mos_ = mo_space_info_->get_corr_abs_mo("RESTRICTED_UOCC");

    if (eri_df_) {
        aux_mos_ = std::vector<size_t>(ints_->nthree());
        std::iota(aux_mos_.begin(), aux_mos_.end(), 0);
    }
}

void MASTER_DSRG::set_ambit_MOSpace() {
    BlockedTensor::reset_mo_spaces();
    BlockedTensor::set_expert_mode(true);

    // define space labels
    acore_label_ = "c";
    aactv_label_ = "a";
    avirt_label_ = "v";
    bcore_label_ = "C";
    bactv_label_ = "A";
    bvirt_label_ = "V";

    // add Ambit index labels
    BTF_->add_mo_space(acore_label_, "mn", core_mos_, AlphaSpin);
    BTF_->add_mo_space(bcore_label_, "MN", core_mos_, BetaSpin);
    BTF_->add_mo_space(aactv_label_, "uvwxyz123", actv_mos_, AlphaSpin);
    BTF_->add_mo_space(bactv_label_, "UVWXYZ!@#", actv_mos_, BetaSpin);
    BTF_->add_mo_space(avirt_label_, "ef", virt_mos_, AlphaSpin);
    BTF_->add_mo_space(bvirt_label_, "EF", virt_mos_, BetaSpin);

    // map space labels to mo spaces
    label_to_spacemo_[acore_label_[0]] = core_mos_;
    label_to_spacemo_[bcore_label_[0]] = core_mos_;
    label_to_spacemo_[aactv_label_[0]] = actv_mos_;
    label_to_spacemo_[bactv_label_[0]] = actv_mos_;
    label_to_spacemo_[avirt_label_[0]] = virt_mos_;
    label_to_spacemo_[bvirt_label_[0]] = virt_mos_;

    // define composite spaces
    BTF_->add_composite_mo_space("h", "ijkl", {acore_label_, aactv_label_});
    BTF_->add_composite_mo_space("H", "IJKL", {bcore_label_, bactv_label_});
    BTF_->add_composite_mo_space("p", "abcd", {aactv_label_, avirt_label_});
    BTF_->add_composite_mo_space("P", "ABCD", {bactv_label_, bvirt_label_});
    BTF_->add_composite_mo_space("g", "pqrs", {acore_label_, aactv_label_, avirt_label_});
    BTF_->add_composite_mo_space("G", "PQRS", {bcore_label_, bactv_label_, bvirt_label_});

    // if DF/CD
    if (eri_df_) {
        aux_label_ = "L";
        BTF_->add_mo_space(aux_label_, "g", aux_mos_, NoSpin);
        label_to_spacemo_[aux_label_[0]] = aux_mos_;
    }
}

void MASTER_DSRG::fill_density() {
    // 1-particle density (link the reference)
    Gamma1_.block("aa") = reference_.L1a();
    Gamma1_.block("AA") = reference_.L1a();

    // 1-hole density
    (Eta1_.block("aa")).iterate([&](const std::vector<size_t>& i, double& value) {
        value = i[0] == i[1] ? 1.0 : 0.0;
    });
    (Eta1_.block("AA")).iterate([&](const std::vector<size_t>& i, double& value) {
        value = i[0] == i[1] ? 1.0 : 0.0;
    });
    Eta1_.block("aa")("pq") -= reference_.L1a()("pq");
    Eta1_.block("AA")("pq") -= reference_.L1a()("pq");

    // 2-body density cumulants (link the reference)
    Lambda2_.block("aaaa") = reference_.L2aa();
    Lambda2_.block("aAaA") = reference_.L2ab();
    Lambda2_.block("AAAA") = reference_.L2bb();
}

void MASTER_DSRG::H1_T1_C0(BlockedTensor& H1, BlockedTensor& T1, const double& alpha, double& C0) {
    Timer timer;

    double E = 0.0;
    E += H1["em"] * T1["me"];
    E += H1["ex"] * T1["ye"] * Gamma1_["xy"];
    E += H1["xm"] * T1["my"] * Eta1_["yx"];

    E += H1["EM"] * T1["ME"];
    E += H1["EX"] * T1["YE"] * Gamma1_["XY"];
    E += H1["XM"] * T1["MY"] * Eta1_["YX"];

    E *= alpha;
    C0 += E;

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H1, T1] -> C0 : %12.3f", timer.get());
    }
    dsrg_time_.add("110", timer.get());
}

void MASTER_DSRG::H1_T2_C0(BlockedTensor& H1, BlockedTensor& T2, const double& alpha, double& C0) {
    Timer timer;
    BlockedTensor temp;
    double E = 0.0;

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aaaa"});
    temp["uvxy"] += H1["ex"] * T2["uvey"];
    temp["uvxy"] -= H1["vm"] * T2["umxy"];
    E += 0.5 * temp["uvxy"] * Lambda2_["xyuv"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"AAAA"});
    temp["UVXY"] += H1["EX"] * T2["UVEY"];
    temp["UVXY"] -= H1["VM"] * T2["UMXY"];
    E += 0.5 * temp["UVXY"] * Lambda2_["XYUV"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aAaA"});
    temp["uVxY"] += H1["ex"] * T2["uVeY"];
    temp["uVxY"] += H1["EY"] * T2["uVxE"];
    temp["uVxY"] -= H1["VM"] * T2["uMxY"];
    temp["uVxY"] -= H1["um"] * T2["mVxY"];
    E += temp["uVxY"] * Lambda2_["xYuV"];

    E *= alpha;
    C0 += E;

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H1, T2] -> C0 : %12.3f", timer.get());
    }
    dsrg_time_.add("120", timer.get());
}

void MASTER_DSRG::H2_T1_C0(BlockedTensor& H2, BlockedTensor& T1, const double& alpha, double& C0) {
    Timer timer;
    BlockedTensor temp;
    double E = 0.0;

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aaaa"});
    temp["uvxy"] += H2["evxy"] * T1["ue"];
    temp["uvxy"] -= H2["uvmy"] * T1["mx"];
    E += 0.5 * temp["uvxy"] * Lambda2_["xyuv"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"AAAA"});
    temp["UVXY"] += H2["EVXY"] * T1["UE"];
    temp["UVXY"] -= H2["UVMY"] * T1["MX"];
    E += 0.5 * temp["UVXY"] * Lambda2_["XYUV"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aAaA"});
    temp["uVxY"] += H2["eVxY"] * T1["ue"];
    temp["uVxY"] += H2["uExY"] * T1["VE"];
    temp["uVxY"] -= H2["uVmY"] * T1["mx"];
    temp["uVxY"] -= H2["uVxM"] * T1["MY"];
    E += temp["uVxY"] * Lambda2_["xYuV"];

    E *= alpha;
    C0 += E;

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H2, T1] -> C0 : %12.3f", timer.get());
    }
    dsrg_time_.add("210", timer.get());
}

void MASTER_DSRG::H2_T2_C0(BlockedTensor& H2, BlockedTensor& T2, const double& alpha, double& C0) {
    Timer timer;

    // <[Hbar2, T2]> (C_2)^4
    double E = H2["eFmN"] * T2["mNeF"];
    E += 0.25 * H2["efmn"] * T2["mnef"];
    E += 0.25 * H2["EFMN"] * T2["MNEF"];

    BlockedTensor temp = ambit::BlockedTensor::build(tensor_type_, "temp", spin_cases({"aa"}));
    temp["vu"] += 0.5 * H2["efmu"] * T2["mvef"];
    temp["vu"] += H2["fEuM"] * T2["vMfE"];
    temp["VU"] += 0.5 * H2["EFMU"] * T2["MVEF"];
    temp["VU"] += H2["eFmU"] * T2["mVeF"];
    E += temp["vu"] * Gamma1_["uv"];
    E += temp["VU"] * Gamma1_["UV"];

    temp.zero();
    temp["vu"] += 0.5 * H2["vemn"] * T2["mnue"];
    temp["vu"] += H2["vEmN"] * T2["mNuE"];
    temp["VU"] += 0.5 * H2["VEMN"] * T2["MNUE"];
    temp["VU"] += H2["eVnM"] * T2["nMeU"];
    E += temp["vu"] * Eta1_["uv"];
    E += temp["VU"] * Eta1_["UV"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", spin_cases({"aaaa"}));
    temp["yvxu"] += H2["efxu"] * T2["yvef"];
    temp["yVxU"] += H2["eFxU"] * T2["yVeF"];
    temp["YVXU"] += H2["EFXU"] * T2["YVEF"];
    E += 0.25 * temp["yvxu"] * Gamma1_["xy"] * Gamma1_["uv"];
    E += temp["yVxU"] * Gamma1_["UV"] * Gamma1_["xy"];
    E += 0.25 * temp["YVXU"] * Gamma1_["XY"] * Gamma1_["UV"];

    temp.zero();
    temp["vyux"] += H2["vymn"] * T2["mnux"];
    temp["vYuX"] += H2["vYmN"] * T2["mNuX"];
    temp["VYUX"] += H2["VYMN"] * T2["MNUX"];
    E += 0.25 * temp["vyux"] * Eta1_["uv"] * Eta1_["xy"];
    E += temp["vYuX"] * Eta1_["uv"] * Eta1_["XY"];
    E += 0.25 * temp["VYUX"] * Eta1_["UV"] * Eta1_["XY"];

    temp.zero();
    temp["vyux"] += H2["vemx"] * T2["myue"];
    temp["vyux"] += H2["vExM"] * T2["yMuE"];
    temp["VYUX"] += H2["eVmX"] * T2["mYeU"];
    temp["VYUX"] += H2["VEXM"] * T2["YMUE"];
    E += temp["vyux"] * Gamma1_["xy"] * Eta1_["uv"];
    E += temp["VYUX"] * Gamma1_["XY"] * Eta1_["UV"];
    temp["yVxU"] = H2["eVxM"] * T2["yMeU"];
    E += temp["yVxU"] * Gamma1_["xy"] * Eta1_["UV"];
    temp["vYuX"] = H2["vEmX"] * T2["mYuE"];
    E += temp["vYuX"] * Gamma1_["XY"] * Eta1_["uv"];

    temp.zero();
    temp["yvxu"] += 0.5 * Gamma1_["wz"] * H2["vexw"] * T2["yzue"];
    temp["yvxu"] += Gamma1_["WZ"] * H2["vExW"] * T2["yZuE"];
    temp["yvxu"] += 0.5 * Eta1_["wz"] * T2["myuw"] * H2["vzmx"];
    temp["yvxu"] += Eta1_["WZ"] * T2["yMuW"] * H2["vZxM"];
    E += temp["yvxu"] * Gamma1_["xy"] * Eta1_["uv"];

    temp["YVXU"] += 0.5 * Gamma1_["WZ"] * H2["VEXW"] * T2["YZUE"];
    temp["YVXU"] += Gamma1_["wz"] * H2["eVwX"] * T2["zYeU"];
    temp["YVXU"] += 0.5 * Eta1_["WZ"] * T2["MYUW"] * H2["VZMX"];
    temp["YVXU"] += Eta1_["wz"] * H2["zVmX"] * T2["mYwU"];
    E += temp["YVXU"] * Gamma1_["XY"] * Eta1_["UV"];

    // <[Hbar2, T2]> C_4 (C_2)^2 HH -- combined with PH
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", spin_cases({"aaaa"}));
    temp["uvxy"] += 0.125 * H2["uvmn"] * T2["mnxy"];
    temp["uvxy"] += 0.25 * Gamma1_["wz"] * H2["uvmw"] * T2["mzxy"];
    temp["uVxY"] += H2["uVmN"] * T2["mNxY"];
    temp["uVxY"] += Gamma1_["wz"] * T2["zMxY"] * H2["uVwM"];
    temp["uVxY"] += Gamma1_["WZ"] * H2["uVmW"] * T2["mZxY"];
    temp["UVXY"] += 0.125 * H2["UVMN"] * T2["MNXY"];
    temp["UVXY"] += 0.25 * Gamma1_["WZ"] * H2["UVMW"] * T2["MZXY"];

    // <[Hbar2, T2]> C_4 (C_2)^2 PP -- combined with PH
    temp["uvxy"] += 0.125 * H2["efxy"] * T2["uvef"];
    temp["uvxy"] += 0.25 * Eta1_["wz"] * T2["uvew"] * H2["ezxy"];
    temp["uVxY"] += H2["eFxY"] * T2["uVeF"];
    temp["uVxY"] += Eta1_["wz"] * H2["zExY"] * T2["uVwE"];
    temp["uVxY"] += Eta1_["WZ"] * T2["uVeW"] * H2["eZxY"];
    temp["UVXY"] += 0.125 * H2["EFXY"] * T2["UVEF"];
    temp["UVXY"] += 0.25 * Eta1_["WZ"] * T2["UVEW"] * H2["EZXY"];

    // <[Hbar2, T2]> C_4 (C_2)^2 PH
    temp["uvxy"] += H2["eumx"] * T2["mvey"];
    temp["uvxy"] += H2["uExM"] * T2["vMyE"];
    temp["uvxy"] += Gamma1_["wz"] * T2["zvey"] * H2["euwx"];
    temp["uvxy"] += Gamma1_["WZ"] * H2["uExW"] * T2["vZyE"];
    temp["uvxy"] += Eta1_["zw"] * H2["wumx"] * T2["mvzy"];
    temp["uvxy"] += Eta1_["ZW"] * T2["vMyZ"] * H2["uWxM"];
    E += temp["uvxy"] * Lambda2_["xyuv"];

    temp["UVXY"] += H2["eUmX"] * T2["mVeY"];
    temp["UVXY"] += H2["EUMX"] * T2["MVEY"];
    temp["UVXY"] += Gamma1_["wz"] * T2["zVeY"] * H2["eUwX"];
    temp["UVXY"] += Gamma1_["WZ"] * T2["ZVEY"] * H2["EUWX"];
    temp["UVXY"] += Eta1_["zw"] * H2["wUmX"] * T2["mVzY"];
    temp["UVXY"] += Eta1_["ZW"] * H2["WUMX"] * T2["MVZY"];
    E += temp["UVXY"] * Lambda2_["XYUV"];

    temp["uVxY"] += H2["uexm"] * T2["mVeY"];
    temp["uVxY"] += H2["uExM"] * T2["MVEY"];
    temp["uVxY"] -= H2["eVxM"] * T2["uMeY"];
    temp["uVxY"] -= H2["uEmY"] * T2["mVxE"];
    temp["uVxY"] += H2["eVmY"] * T2["umxe"];
    temp["uVxY"] += H2["EVMY"] * T2["uMxE"];

    temp["uVxY"] += Gamma1_["wz"] * T2["zVeY"] * H2["uexw"];
    temp["uVxY"] += Gamma1_["WZ"] * T2["ZVEY"] * H2["uExW"];
    temp["uVxY"] -= Gamma1_["WZ"] * H2["eVxW"] * T2["uZeY"];
    temp["uVxY"] -= Gamma1_["wz"] * T2["zVxE"] * H2["uEwY"];
    temp["uVxY"] += Gamma1_["wz"] * T2["zuex"] * H2["eVwY"];
    temp["uVxY"] -= Gamma1_["WZ"] * H2["EVYW"] * T2["uZxE"];

    temp["uVxY"] += Eta1_["zw"] * H2["wumx"] * T2["mVzY"];
    temp["uVxY"] += Eta1_["ZW"] * T2["VMYZ"] * H2["uWxM"];
    temp["uVxY"] -= Eta1_["zw"] * H2["wVxM"] * T2["uMzY"];
    temp["uVxY"] -= Eta1_["ZW"] * T2["mVxZ"] * H2["uWmY"];
    temp["uVxY"] += Eta1_["zw"] * T2["umxz"] * H2["wVmY"];
    temp["uVxY"] += Eta1_["ZW"] * H2["WVMY"] * T2["uMxZ"];
    E += temp["uVxY"] * Lambda2_["xYuV"];

    // <[Hbar2, T2]> C_6 C_2
    if (options_.get_str("THREEPDC") != "ZERO") {
        temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aaaaaa"});
        temp["uvwxyz"] += H2["uviz"] * T2["iwxy"];
        temp["uvwxyz"] += H2["waxy"] * T2["uvaz"];
        E += 0.25 * temp.block("aaaaaa")("uvwxyz") * reference_.L3aaa()("xyzuvw");

        temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"AAAAAA"});
        temp["UVWXYZ"] += H2["UVIZ"] * T2["IWXY"];
        temp["UVWXYZ"] += H2["WAXY"] * T2["UVAZ"];
        E += 0.25 * temp.block("AAAAAA")("UVWXYZ") * reference_.L3bbb()("XYZUVW");

        temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aaAaaA"});
        temp["uvWxyZ"] -= H2["uviy"] * T2["iWxZ"];
        temp["uvWxyZ"] -= H2["uWiZ"] * T2["ivxy"];
        temp["uvWxyZ"] += 2.0 * H2["uWyI"] * T2["vIxZ"];

        temp["uvWxyZ"] += H2["aWxZ"] * T2["uvay"];
        temp["uvWxyZ"] -= H2["vaxy"] * T2["uWaZ"];
        temp["uvWxyZ"] -= 2.0 * H2["vAxZ"] * T2["uWyA"];
        E += 0.5 * temp.block("aaAaaA")("uvWxyZ") * reference_.L3aab()("xyZuvW");

        temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aAAaAA"});
        temp["uVWxYZ"] -= H2["VWIZ"] * T2["uIxY"];
        temp["uVWxYZ"] -= H2["uVxI"] * T2["IWYZ"];
        temp["uVWxYZ"] += 2.0 * H2["uViZ"] * T2["iWxY"];

        temp["uVWxYZ"] += H2["uAxY"] * T2["VWAZ"];
        temp["uVWxYZ"] -= H2["WAYZ"] * T2["uVxA"];
        temp["uVWxYZ"] -= 2.0 * H2["aWxY"] * T2["uVaZ"];
        E += 0.5 * temp.block("aAAaAA")("uVWxYZ") * reference_.L3abb()("xYZuVW");
    }

    // multiply prefactor and copy to C0
    E *= alpha;
    C0 += E;

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H2, T2] -> C0 : %12.3f", timer.get());
    }
    dsrg_time_.add("220", timer.get());
}

void MASTER_DSRG::H1_T1_C1(BlockedTensor& H1, BlockedTensor& T1, const double& alpha,
                           BlockedTensor& C1) {
    Timer timer;

    C1["ip"] += alpha * H1["ap"] * T1["ia"];
    C1["qa"] -= alpha * T1["ia"] * H1["qi"];
    C1["IP"] += alpha * H1["AP"] * T1["IA"];
    C1["QA"] -= alpha * T1["IA"] * H1["QI"];

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H1, T1] -> C1 : %12.3f", timer.get());
    }
    dsrg_time_.add("111", timer.get());
}

// void MASTER_DSRG::H1_T1_C1aa(BlockedTensor& H1, BlockedTensor& T1, const double& alpha,
//                             BlockedTensor& C1) {
//    Timer timer;

//    C1["uv"] += alpha * H1["av"] * T1["ua"];
//    C1["uv"] -= alpha * T1["iv"] * H1["ui"];
//    C1["UV"] += alpha * H1["AV"] * T1["UA"];
//    C1["UV"] -= alpha * T1["IV"] * H1["UI"];

//    if (print_ > 2) {
//        outfile->Printf("\n    Time for [H1, T1] -> C1aa : %12.3f", timer.get());
//    }
//    dsrg_time_.add("111", timer.get());
//}

// void MASTER_DSRG::H1_T1_C1ph(BlockedTensor& H1, BlockedTensor& T1, const double& alpha,
//                             BlockedTensor& C1) {
//    Timer timer;

//    C1["ui"] += alpha * H1["ai"] * T1["ua"];
//    C1["au"] -= alpha * T1["iu"] * H1["ai"];
//    C1["UI"] += alpha * H1["AI"] * T1["UA"];
//    C1["AU"] -= alpha * T1["IU"] * H1["AI"];

//    if (print_ > 2) {
//        outfile->Printf("\n    Time for [H1, T1] -> C1ph : %12.3f", timer.get());
//    }
//    dsrg_time_.add("111", timer.get());
//}

// void MASTER_DSRG::H1_T1_C1hp(BlockedTensor& H1, BlockedTensor& T1, const double& alpha,
//                             BlockedTensor& C1) {
//    Timer timer;

//    C1["ib"] += alpha * H1["ab"] * T1["ia"];
//    C1["ja"] -= alpha * T1["ia"] * H1["ji"];
//    C1["IB"] += alpha * H1["AB"] * T1["IA"];
//    C1["JA"] -= alpha * T1["IA"] * H1["JI"];

//    if (print_ > 2) {
//        outfile->Printf("\n    Time for [H1, T1] -> C1hp : %12.3f", timer.get());
//    }
//    dsrg_time_.add("111", timer.get());
//}

void MASTER_DSRG::H1_T2_C1(BlockedTensor& H1, BlockedTensor& T2, const double& alpha,
                           BlockedTensor& C1) {
    Timer timer;

    C1["ia"] += alpha * H1["bm"] * T2["imab"];
    C1["ia"] += alpha * H1["bu"] * Gamma1_["uv"] * T2["ivab"];
    C1["ia"] -= alpha * H1["vj"] * Gamma1_["uv"] * T2["ijau"];
    C1["ia"] += alpha * H1["BM"] * T2["iMaB"];
    C1["ia"] += alpha * H1["BU"] * Gamma1_["UV"] * T2["iVaB"];
    C1["ia"] -= alpha * H1["VJ"] * Gamma1_["UV"] * T2["iJaU"];

    C1["IA"] += alpha * H1["bm"] * T2["mIbA"];
    C1["IA"] += alpha * H1["bu"] * Gamma1_["uv"] * T2["vIbA"];
    C1["IA"] -= alpha * H1["vj"] * Gamma1_["uv"] * T2["jIuA"];
    C1["IA"] += alpha * H1["BM"] * T2["IMAB"];
    C1["IA"] += alpha * H1["BU"] * Gamma1_["UV"] * T2["IVAB"];
    C1["IA"] -= alpha * H1["VJ"] * Gamma1_["UV"] * T2["IJAU"];

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H1, T2] -> C1 : %12.3f", timer.get());
    }
    dsrg_time_.add("121", timer.get());
}

// void MASTER_DSRG::H1_T2_C1aa(BlockedTensor& H1, BlockedTensor& T2, const double& alpha,
//                             BlockedTensor& C1) {
//    Timer timer;

//    C1["xy"] += alpha * H1["bm"] * T2["xmyb"];
//    C1["xy"] += alpha * H1["bu"] * Gamma1_["uv"] * T2["xvyb"];
//    C1["xy"] -= alpha * H1["vj"] * Gamma1_["uv"] * T2["xjyu"];
//    C1["xy"] += alpha * H1["BM"] * T2["xMyB"];
//    C1["xy"] += alpha * H1["BU"] * Gamma1_["UV"] * T2["xVyB"];
//    C1["xy"] -= alpha * H1["VJ"] * Gamma1_["UV"] * T2["xJyU"];

//    C1["XY"] += alpha * H1["bm"] * T2["mXbY"];
//    C1["XY"] += alpha * H1["bu"] * Gamma1_["uv"] * T2["vXbY"];
//    C1["XY"] -= alpha * H1["vj"] * Gamma1_["uv"] * T2["jXuY"];
//    C1["XY"] += alpha * H1["BM"] * T2["XMYB"];
//    C1["XY"] += alpha * H1["BU"] * Gamma1_["UV"] * T2["XVYB"];
//    C1["XY"] -= alpha * H1["VJ"] * Gamma1_["UV"] * T2["XJYU"];

//    if (print_ > 2) {
//        outfile->Printf("\n    Time for [H1, T2] -> C1aa : %12.3f", timer.get());
//    }
//    dsrg_time_.add("121", timer.get());
//}

// void MASTER_DSRG::H1_T2_C1ph(BlockedTensor& H1, BlockedTensor& T2, const double& alpha,
//                             BlockedTensor& C1) {
//    H1_T2_C1aa(H1, T2, alpha, C1);
//}

// void MASTER_DSRG::H1_T2_C1hp(BlockedTensor& H1, BlockedTensor& T2, const double& alpha,
//                             BlockedTensor& C1) {
//    H1_T2_C1(H1, T2, alpha, C1);
//}

void MASTER_DSRG::H2_T1_C1(BlockedTensor& H2, BlockedTensor& T1, const double& alpha,
                           BlockedTensor& C1) {
    Timer timer;

    C1["qp"] += alpha * T1["ma"] * H2["qapm"];
    C1["qp"] += alpha * T1["xe"] * Gamma1_["yx"] * H2["qepy"];
    C1["qp"] -= alpha * T1["mu"] * Gamma1_["uv"] * H2["qvpm"];
    C1["qp"] += alpha * T1["MA"] * H2["qApM"];
    C1["qp"] += alpha * T1["XE"] * Gamma1_["YX"] * H2["qEpY"];
    C1["qp"] -= alpha * T1["MU"] * Gamma1_["UV"] * H2["qVpM"];

    C1["QP"] += alpha * T1["ma"] * H2["aQmP"];
    C1["QP"] += alpha * T1["xe"] * Gamma1_["yx"] * H2["eQyP"];
    C1["QP"] -= alpha * T1["mu"] * Gamma1_["uv"] * H2["vQmP"];
    C1["QP"] += alpha * T1["MA"] * H2["QAPM"];
    C1["QP"] += alpha * T1["XE"] * Gamma1_["YX"] * H2["QEPY"];
    C1["QP"] -= alpha * T1["MU"] * Gamma1_["UV"] * H2["QVPM"];

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H2, T1] -> C1 : %12.3f", timer.get());
    }
    dsrg_time_.add("211", timer.get());
}

// void MASTER_DSRG::H2_T1_C1aa(BlockedTensor& H2, BlockedTensor& T1, const double& alpha,
//                             BlockedTensor& C1) {
//    Timer timer;

//    C1["xy"] += alpha * T1["ma"] * H2["xaym"];
//    C1["xy"] += alpha * T1["ue"] * Gamma1_["vu"] * H2["xeyv"];
//    C1["xy"] -= alpha * T1["mu"] * Gamma1_["uv"] * H2["xvym"];
//    C1["xy"] += alpha * T1["MA"] * H2["xAyM"];
//    C1["xy"] += alpha * T1["UE"] * Gamma1_["VU"] * H2["xEyV"];
//    C1["xy"] -= alpha * T1["MU"] * Gamma1_["UV"] * H2["xVyM"];

//    C1["XY"] += alpha * T1["ma"] * H2["aXmY"];
//    C1["XY"] += alpha * T1["ue"] * Gamma1_["vu"] * H2["eXvY"];
//    C1["XY"] -= alpha * T1["mu"] * Gamma1_["uv"] * H2["vXmY"];
//    C1["XY"] += alpha * T1["MA"] * H2["XAYM"];
//    C1["XY"] += alpha * T1["UE"] * Gamma1_["VU"] * H2["XEYV"];
//    C1["XY"] -= alpha * T1["MU"] * Gamma1_["UV"] * H2["XVYM"];

//    if (print_ > 2) {
//        outfile->Printf("\n    Time for [H2, T1] -> C1aa : %12.3f", timer.get());
//    }
//    dsrg_time_.add("211", timer.get());
//}

// void MASTER_DSRG::H2_T1_C1hp(BlockedTensor& H2, BlockedTensor& T1, const double& alpha,
//                             BlockedTensor& C1) {
//    Timer timer;

//    C1["kc"] += alpha * T1["ma"] * H2["kacm"];
//    C1["kc"] += alpha * T1["xe"] * Gamma1_["yx"] * H2["kecy"];
//    C1["kc"] -= alpha * T1["mu"] * Gamma1_["uv"] * H2["kvcm"];
//    C1["kc"] += alpha * T1["MA"] * H2["kAcM"];
//    C1["kc"] += alpha * T1["XE"] * Gamma1_["YX"] * H2["kEcY"];
//    C1["kc"] -= alpha * T1["MU"] * Gamma1_["UV"] * H2["kVcM"];

//    C1["KC"] += alpha * T1["ma"] * H2["aKmC"];
//    C1["KC"] += alpha * T1["xe"] * Gamma1_["yx"] * H2["eKyC"];
//    C1["KC"] -= alpha * T1["mu"] * Gamma1_["uv"] * H2["vKmC"];
//    C1["KC"] += alpha * T1["MA"] * H2["KACM"];
//    C1["KC"] += alpha * T1["XE"] * Gamma1_["YX"] * H2["KECY"];
//    C1["KC"] -= alpha * T1["MU"] * Gamma1_["UV"] * H2["KVCM"];

//    if (print_ > 2) {
//        outfile->Printf("\n    Time for [H2, T1] -> C1hp : %12.3f", timer.get());
//    }
//    dsrg_time_.add("211", timer.get());
//}

// void MASTER_DSRG::H2_T1_C1ph(BlockedTensor& H2, BlockedTensor& T1, const double& alpha,
//                           BlockedTensor& C1) {
//    Timer timer;

//    C1["ck"] += alpha * T1["ma"] * H2["cakm"];
//    C1["ck"] += alpha * T1["xe"] * Gamma1_["yx"] * H2["ceky"];
//    C1["ck"] -= alpha * T1["mu"] * Gamma1_["uv"] * H2["cvkm"];
//    C1["ck"] += alpha * T1["MA"] * H2["cAkM"];
//    C1["ck"] += alpha * T1["XE"] * Gamma1_["YX"] * H2["cEkY"];
//    C1["ck"] -= alpha * T1["MU"] * Gamma1_["UV"] * H2["cVkM"];

//    C1["CK"] += alpha * T1["ma"] * H2["aCmK"];
//    C1["CK"] += alpha * T1["xe"] * Gamma1_["yx"] * H2["eCyK"];
//    C1["CK"] -= alpha * T1["mu"] * Gamma1_["uv"] * H2["vCmK"];
//    C1["CK"] += alpha * T1["MA"] * H2["CAKM"];
//    C1["CK"] += alpha * T1["XE"] * Gamma1_["YX"] * H2["CEKY"];
//    C1["CK"] -= alpha * T1["MU"] * Gamma1_["UV"] * H2["CVKM"];

//    if (print_ > 2) {
//        outfile->Printf("\n    Time for [H2, T1] -> C1ph : %12.3f", timer.get());
//    }
//    dsrg_time_.add("211", timer.get());
//}

void MASTER_DSRG::H2_T2_C1(BlockedTensor& H2, BlockedTensor& T2, const double& alpha,
                           BlockedTensor& C1) {
    Timer timer;
    BlockedTensor temp;

    /// minimum memory requirement: h * a * p * p

    // [Hbar2, T2] (C_2)^3 -> C1 particle contractions
    C1["ir"] += 0.5 * alpha * H2["abrm"] * T2["imab"];
    C1["ir"] += alpha * H2["aBrM"] * T2["iMaB"];
    C1["IR"] += 0.5 * alpha * H2["ABRM"] * T2["IMAB"];
    C1["IR"] += alpha * H2["aBmR"] * T2["mIaB"];

    C1["ir"] += 0.5 * alpha * Gamma1_["uv"] * T2["ivab"] * H2["abru"];
    C1["ir"] += alpha * Gamma1_["UV"] * T2["iVaB"] * H2["aBrU"];
    C1["IR"] += 0.5 * alpha * Gamma1_["UV"] * T2["IVAB"] * H2["ABRU"];
    C1["IR"] += alpha * Gamma1_["uv"] * T2["vIaB"] * H2["aBuR"];

    C1["ir"] += 0.5 * alpha * T2["ijux"] * Gamma1_["xy"] * Gamma1_["uv"] * H2["vyrj"];
    C1["IR"] += 0.5 * alpha * T2["IJUX"] * Gamma1_["XY"] * Gamma1_["UV"] * H2["VYRJ"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"hHaA"});
    temp["iJvY"] = T2["iJuX"] * Gamma1_["XY"] * Gamma1_["uv"];
    C1["ir"] += alpha * temp["iJvY"] * H2["vYrJ"];
    C1["IR"] += alpha * temp["jIvY"] * H2["vYjR"];

    C1["ir"] -= alpha * Gamma1_["uv"] * T2["imub"] * H2["vbrm"];
    C1["ir"] -= alpha * Gamma1_["uv"] * T2["iMuB"] * H2["vBrM"];
    C1["ir"] -= alpha * Gamma1_["UV"] * T2["iMbU"] * H2["bVrM"];
    C1["IR"] -= alpha * Gamma1_["UV"] * T2["IMUB"] * H2["VBRM"];
    C1["IR"] -= alpha * Gamma1_["UV"] * T2["mIbU"] * H2["bVmR"];
    C1["IR"] -= alpha * Gamma1_["uv"] * T2["mIuB"] * H2["vBmR"];

    C1["ir"] -= alpha * T2["iyub"] * Gamma1_["uv"] * Gamma1_["xy"] * H2["vbrx"];
    C1["ir"] -= alpha * T2["iYuB"] * Gamma1_["uv"] * Gamma1_["XY"] * H2["vBrX"];
    C1["ir"] -= alpha * T2["iYbU"] * Gamma1_["XY"] * Gamma1_["UV"] * H2["bVrX"];
    C1["IR"] -= alpha * T2["IYUB"] * Gamma1_["UV"] * Gamma1_["XY"] * H2["VBRX"];
    C1["IR"] -= alpha * T2["yIuB"] * Gamma1_["uv"] * Gamma1_["xy"] * H2["vBxR"];
    C1["IR"] -= alpha * T2["yIbU"] * Gamma1_["UV"] * Gamma1_["xy"] * H2["bVxR"];

    // [Hbar2, T2] (C_2)^3 -> C1 hole contractions
    C1["pa"] -= 0.5 * alpha * H2["peij"] * T2["ijae"];
    C1["pa"] -= alpha * H2["pEiJ"] * T2["iJaE"];
    C1["PA"] -= 0.5 * alpha * H2["PEIJ"] * T2["IJAE"];
    C1["PA"] -= alpha * H2["ePiJ"] * T2["iJeA"];

    C1["pa"] -= 0.5 * alpha * Eta1_["uv"] * T2["ijau"] * H2["pvij"];
    C1["pa"] -= alpha * Eta1_["UV"] * T2["iJaU"] * H2["pViJ"];
    C1["PA"] -= 0.5 * alpha * Eta1_["UV"] * T2["IJAU"] * H2["PVIJ"];
    C1["PA"] -= alpha * Eta1_["uv"] * T2["iJuA"] * H2["vPiJ"];

    C1["pa"] -= 0.5 * alpha * T2["vyab"] * Eta1_["uv"] * Eta1_["xy"] * H2["pbux"];
    C1["PA"] -= 0.5 * alpha * T2["VYAB"] * Eta1_["UV"] * Eta1_["XY"] * H2["PBUX"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aApP"});
    temp["uXaB"] = T2["vYaB"] * Eta1_["uv"] * Eta1_["XY"];
    C1["pa"] -= alpha * H2["pBuX"] * temp["uXaB"];
    C1["PA"] -= alpha * H2["bPuX"] * temp["uXbA"];

    C1["pa"] += alpha * Eta1_["uv"] * T2["vjae"] * H2["peuj"];
    C1["pa"] += alpha * Eta1_["uv"] * T2["vJaE"] * H2["pEuJ"];
    C1["pa"] += alpha * Eta1_["UV"] * T2["jVaE"] * H2["pEjU"];
    C1["PA"] += alpha * Eta1_["UV"] * T2["VJAE"] * H2["PEUJ"];
    C1["PA"] += alpha * Eta1_["uv"] * T2["vJeA"] * H2["ePuJ"];
    C1["PA"] += alpha * Eta1_["UV"] * T2["jVeA"] * H2["ePjU"];

    C1["pa"] += alpha * T2["vjax"] * Eta1_["uv"] * Eta1_["xy"] * H2["pyuj"];
    C1["pa"] += alpha * T2["vJaX"] * Eta1_["uv"] * Eta1_["XY"] * H2["pYuJ"];
    C1["pa"] += alpha * T2["jVaX"] * Eta1_["XY"] * Eta1_["UV"] * H2["pYjU"];
    C1["PA"] += alpha * T2["VJAX"] * Eta1_["UV"] * Eta1_["XY"] * H2["PYUJ"];
    C1["PA"] += alpha * T2["vJxA"] * Eta1_["uv"] * Eta1_["xy"] * H2["yPuJ"];
    C1["PA"] += alpha * T2["jVxA"] * Eta1_["UV"] * Eta1_["xy"] * H2["yPjU"];

    // [Hbar2, T2] C_4 C_2 2:2 -> C1
    C1["ir"] += 0.25 * alpha * T2["ijxy"] * Lambda2_["xyuv"] * H2["uvrj"];
    C1["IR"] += 0.25 * alpha * T2["IJXY"] * Lambda2_["XYUV"] * H2["UVRJ"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"hHaA"});
    temp["iJuV"] = T2["iJxY"] * Lambda2_["xYuV"];
    C1["ir"] += alpha * H2["uVrJ"] * temp["iJuV"];
    C1["IR"] += alpha * H2["uVjR"] * temp["jIuV"];

    C1["pa"] -= 0.25 * alpha * Lambda2_["xyuv"] * T2["uvab"] * H2["pbxy"];
    C1["PA"] -= 0.25 * alpha * Lambda2_["XYUV"] * T2["UVAB"] * H2["PBXY"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aApP"});
    temp["xYaB"] = T2["uVaB"] * Lambda2_["xYuV"];
    C1["pa"] -= alpha * H2["pBxY"] * temp["xYaB"];
    C1["PA"] -= alpha * H2["bPxY"] * temp["xYbA"];

    C1["ir"] -= alpha * Lambda2_["yXuV"] * T2["iVyA"] * H2["uArX"];
    C1["IR"] -= alpha * Lambda2_["xYvU"] * T2["vIaY"] * H2["aUxR"];
    C1["pa"] += alpha * Lambda2_["xYvU"] * T2["vIaY"] * H2["pUxI"];
    C1["PA"] += alpha * Lambda2_["yXuV"] * T2["iVyA"] * H2["uPiX"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"hapa"});
    temp["ixau"] += Lambda2_["xyuv"] * T2["ivay"];
    temp["ixau"] += Lambda2_["xYuV"] * T2["iVaY"];
    C1["ir"] += alpha * temp["ixau"] * H2["aurx"];
    C1["pa"] -= alpha * H2["puix"] * temp["ixau"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"hApA"});
    temp["iXaU"] += Lambda2_["XYUV"] * T2["iVaY"];
    temp["iXaU"] += Lambda2_["yXvU"] * T2["ivay"];
    C1["ir"] += alpha * temp["iXaU"] * H2["aUrX"];
    C1["pa"] -= alpha * H2["pUiX"] * temp["iXaU"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"aHaP"});
    temp["xIuA"] += Lambda2_["xyuv"] * T2["vIyA"];
    temp["xIuA"] += Lambda2_["xYuV"] * T2["VIYA"];
    C1["IR"] += alpha * temp["xIuA"] * H2["uAxR"];
    C1["PA"] -= alpha * H2["uPxI"] * temp["xIuA"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"HAPA"});
    temp["IXAU"] += Lambda2_["XYUV"] * T2["IVAY"];
    temp["IXAU"] += Lambda2_["yXvU"] * T2["vIyA"];
    C1["IR"] += alpha * temp["IXAU"] * H2["AURX"];
    C1["PA"] -= alpha * H2["PUIX"] * temp["IXAU"];

    // [Hbar2, T2] C_4 C_2 1:3 -> C1
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"pa"});
    temp["au"] += 0.5 * Lambda2_["xyuv"] * H2["avxy"];
    temp["au"] += Lambda2_["xYuV"] * H2["aVxY"];
    C1["jb"] += alpha * temp["au"] * T2["ujab"];
    C1["JB"] += alpha * temp["au"] * T2["uJaB"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"PA"});
    temp["AU"] += 0.5 * Lambda2_["XYUV"] * H2["AVXY"];
    temp["AU"] += Lambda2_["xYvU"] * H2["vAxY"];
    C1["jb"] += alpha * temp["AU"] * T2["jUbA"];
    C1["JB"] += alpha * temp["AU"] * T2["UJAB"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"ah"});
    temp["xi"] += 0.5 * Lambda2_["xyuv"] * H2["uviy"];
    temp["xi"] += Lambda2_["xYuV"] * H2["uViY"];
    C1["jb"] -= alpha * temp["xi"] * T2["ijxb"];
    C1["JB"] -= alpha * temp["xi"] * T2["iJxB"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"AH"});
    temp["XI"] += 0.5 * Lambda2_["XYUV"] * H2["UVIY"];
    temp["XI"] += Lambda2_["yXvU"] * H2["vUyI"];
    C1["jb"] -= alpha * temp["XI"] * T2["jIbX"];
    C1["JB"] -= alpha * temp["XI"] * T2["IJXB"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"av"});
    temp["xe"] += 0.5 * T2["uvey"] * Lambda2_["xyuv"];
    temp["xe"] += T2["uVeY"] * Lambda2_["xYuV"];
    C1["qs"] += alpha * temp["xe"] * H2["eqxs"];
    C1["QS"] += alpha * temp["xe"] * H2["eQxS"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"AV"});
    temp["XE"] += 0.5 * T2["UVEY"] * Lambda2_["XYUV"];
    temp["XE"] += T2["uVyE"] * Lambda2_["yXuV"];
    C1["qs"] += alpha * temp["XE"] * H2["qEsX"];
    C1["QS"] += alpha * temp["XE"] * H2["EQXS"];

    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"ca"});
    temp["mu"] += 0.5 * T2["mvxy"] * Lambda2_["xyuv"];
    temp["mu"] += T2["mVxY"] * Lambda2_["xYuV"];
    C1["qs"] -= alpha * temp["mu"] * H2["uqms"];
    C1["QS"] -= alpha * temp["mu"] * H2["uQmS"];
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", {"CA"});
    temp["MU"] += 0.5 * T2["MVXY"] * Lambda2_["XYUV"];
    temp["MU"] += T2["vMxY"] * Lambda2_["xYvU"];
    C1["qs"] -= alpha * temp["MU"] * H2["qUsM"];
    C1["QS"] -= alpha * temp["MU"] * H2["UQMS"];

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H2, T2] -> C1 : %12.3f", timer.get());
    }
    dsrg_time_.add("221", timer.get());
}

void MASTER_DSRG::H1_T2_C2(BlockedTensor& H1, BlockedTensor& T2, const double& alpha,
                           BlockedTensor& C2) {
    Timer timer;

    C2["ijpb"] += alpha * T2["ijab"] * H1["ap"];
    C2["ijap"] += alpha * T2["ijab"] * H1["bp"];
    C2["qjab"] -= alpha * T2["ijab"] * H1["qi"];
    C2["iqab"] -= alpha * T2["ijab"] * H1["qj"];

    C2["iJpB"] += alpha * T2["iJaB"] * H1["ap"];
    C2["iJaP"] += alpha * T2["iJaB"] * H1["BP"];
    C2["qJaB"] -= alpha * T2["iJaB"] * H1["qi"];
    C2["iQaB"] -= alpha * T2["iJaB"] * H1["QJ"];

    C2["IJPB"] += alpha * T2["IJAB"] * H1["AP"];
    C2["IJAP"] += alpha * T2["IJAB"] * H1["BP"];
    C2["QJAB"] -= alpha * T2["IJAB"] * H1["QI"];
    C2["IQAB"] -= alpha * T2["IJAB"] * H1["QJ"];

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H1, T2] -> C2 : %12.3f", timer.get());
    }
    dsrg_time_.add("122", timer.get());
}

void MASTER_DSRG::H2_T1_C2(BlockedTensor& H2, BlockedTensor& T1, const double& alpha,
                           BlockedTensor& C2) {
    Timer timer;

    C2["irpq"] += alpha * T1["ia"] * H2["arpq"];
    C2["ripq"] += alpha * T1["ia"] * H2["rapq"];
    C2["rsaq"] -= alpha * T1["ia"] * H2["rsiq"];
    C2["rspa"] -= alpha * T1["ia"] * H2["rspi"];

    C2["iRpQ"] += alpha * T1["ia"] * H2["aRpQ"];
    C2["rIpQ"] += alpha * T1["IA"] * H2["rApQ"];
    C2["rSaQ"] -= alpha * T1["ia"] * H2["rSiQ"];
    C2["rSpA"] -= alpha * T1["IA"] * H2["rSpI"];

    C2["IRPQ"] += alpha * T1["IA"] * H2["ARPQ"];
    C2["RIPQ"] += alpha * T1["IA"] * H2["RAPQ"];
    C2["RSAQ"] -= alpha * T1["IA"] * H2["RSIQ"];
    C2["RSPA"] -= alpha * T1["IA"] * H2["RSPI"];

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H2, T1] -> C2 : %12.3f", timer.get());
    }
    dsrg_time_.add("212", timer.get());
}

void MASTER_DSRG::H2_T2_C2(BlockedTensor& H2, BlockedTensor& T2, const double& alpha,
                           BlockedTensor& C2) {
    Timer timer;

    /// minimum memory requirement: g * g * p * p

    // particle-particle contractions
    C2["ijrs"] += 0.5 * alpha * H2["abrs"] * T2["ijab"];
    C2["iJrS"] += alpha * H2["aBrS"] * T2["iJaB"];
    C2["IJRS"] += 0.5 * alpha * H2["ABRS"] * T2["IJAB"];

    C2["ijrs"] -= alpha * Gamma1_["xy"] * T2["ijxb"] * H2["ybrs"];
    C2["iJrS"] -= alpha * Gamma1_["xy"] * T2["iJxB"] * H2["yBrS"];
    C2["iJrS"] -= alpha * Gamma1_["XY"] * T2["iJbX"] * H2["bYrS"];
    C2["IJRS"] -= alpha * Gamma1_["XY"] * T2["IJXB"] * H2["YBRS"];

    // hole-hole contractions
    C2["pqab"] += 0.5 * alpha * H2["pqij"] * T2["ijab"];
    C2["pQaB"] += alpha * H2["pQiJ"] * T2["iJaB"];
    C2["PQAB"] += 0.5 * alpha * H2["PQIJ"] * T2["IJAB"];

    C2["pqab"] -= alpha * Eta1_["xy"] * T2["yjab"] * H2["pqxj"];
    C2["pQaB"] -= alpha * Eta1_["xy"] * T2["yJaB"] * H2["pQxJ"];
    C2["pQaB"] -= alpha * Eta1_["XY"] * T2["jYaB"] * H2["pQjX"];
    C2["PQAB"] -= alpha * Eta1_["XY"] * T2["YJAB"] * H2["PQXJ"];

    // hole-particle contractions
    // figure out useful blocks of temp (assume symmetric C2 blocks, if cavv exists => acvv exists)
    std::vector<std::string> C2_blocks(C2.block_labels());
    std::sort(C2_blocks.begin(), C2_blocks.end());
    std::vector<std::string> temp_blocks;
    for (const std::string& p : {"c", "a", "v"}) {
        for (const std::string& q : {"c", "a"}) {
            for (const std::string& r : {"c", "a", "v"}) {
                for (const std::string& s : {"a", "v"}) {
                    temp_blocks.emplace_back(p + q + r + s);
                }
            }
        }
    }
    std::sort(temp_blocks.begin(), temp_blocks.end());
    std::vector<std::string> blocks;
    std::set_intersection(temp_blocks.begin(), temp_blocks.end(), C2_blocks.begin(),
                          C2_blocks.end(), std::back_inserter(blocks));
    BlockedTensor temp = ambit::BlockedTensor::build(tensor_type_, "temp", blocks);
    temp["qjsb"] += alpha * H2["aqms"] * T2["mjab"];
    temp["qjsb"] += alpha * H2["qAsM"] * T2["jMbA"];
    temp["qjsb"] += alpha * Gamma1_["xy"] * T2["yjab"] * H2["aqxs"];
    temp["qjsb"] += alpha * Gamma1_["XY"] * T2["jYbA"] * H2["qAsX"];
    temp["qjsb"] -= alpha * Gamma1_["xy"] * T2["ijxb"] * H2["yqis"];
    temp["qjsb"] -= alpha * Gamma1_["XY"] * T2["jIbX"] * H2["qYsI"];
    C2["qjsb"] += temp["qjsb"];
    C2["jqsb"] -= temp["qjsb"];
    C2["qjbs"] -= temp["qjsb"];
    C2["jqbs"] += temp["qjsb"];

    // figure out useful blocks of temp (assume symmetric C2 blocks, if cavv exists => acvv exists)
    temp_blocks.clear();
    for (const std::string& p : {"C", "A", "V"}) {
        for (const std::string& q : {"C", "A"}) {
            for (const std::string& r : {"C", "A", "V"}) {
                for (const std::string& s : {"A", "V"}) {
                    temp_blocks.emplace_back(p + q + r + s);
                }
            }
        }
    }
    std::sort(temp_blocks.begin(), temp_blocks.end());
    blocks.clear();
    std::set_intersection(temp_blocks.begin(), temp_blocks.end(), C2_blocks.begin(),
                          C2_blocks.end(), std::back_inserter(blocks));
    temp = ambit::BlockedTensor::build(tensor_type_, "temp", blocks);
    temp["QJSB"] += alpha * H2["AQMS"] * T2["MJAB"];
    temp["QJSB"] += alpha * H2["aQmS"] * T2["mJaB"];
    temp["QJSB"] += alpha * Gamma1_["XY"] * T2["YJAB"] * H2["AQXS"];
    temp["QJSB"] += alpha * Gamma1_["xy"] * T2["yJaB"] * H2["aQxS"];
    temp["QJSB"] -= alpha * Gamma1_["XY"] * T2["IJXB"] * H2["YQIS"];
    temp["QJSB"] -= alpha * Gamma1_["xy"] * T2["iJxB"] * H2["yQiS"];
    C2["QJSB"] += temp["QJSB"];
    C2["JQSB"] -= temp["QJSB"];
    C2["QJBS"] -= temp["QJSB"];
    C2["JQBS"] += temp["QJSB"];

    C2["qJsB"] += alpha * H2["aqms"] * T2["mJaB"];
    C2["qJsB"] += alpha * H2["qAsM"] * T2["MJAB"];
    C2["qJsB"] += alpha * Gamma1_["xy"] * T2["yJaB"] * H2["aqxs"];
    C2["qJsB"] += alpha * Gamma1_["XY"] * T2["YJAB"] * H2["qAsX"];
    C2["qJsB"] -= alpha * Gamma1_["xy"] * T2["iJxB"] * H2["yqis"];
    C2["qJsB"] -= alpha * Gamma1_["XY"] * T2["IJXB"] * H2["qYsI"];

    C2["iQsB"] -= alpha * T2["iMaB"] * H2["aQsM"];
    C2["iQsB"] -= alpha * Gamma1_["XY"] * T2["iYaB"] * H2["aQsX"];
    C2["iQsB"] += alpha * Gamma1_["xy"] * T2["iJxB"] * H2["yQsJ"];

    C2["qJaS"] -= alpha * T2["mJaB"] * H2["qBmS"];
    C2["qJaS"] -= alpha * Gamma1_["xy"] * T2["yJaB"] * H2["qBxS"];
    C2["qJaS"] += alpha * Gamma1_["XY"] * T2["iJaX"] * H2["qYiS"];

    C2["iQaS"] += alpha * T2["imab"] * H2["bQmS"];
    C2["iQaS"] += alpha * T2["iMaB"] * H2["BQMS"];
    C2["iQaS"] += alpha * Gamma1_["xy"] * T2["iyab"] * H2["bQxS"];
    C2["iQaS"] += alpha * Gamma1_["XY"] * T2["iYaB"] * H2["BQXS"];
    C2["iQaS"] -= alpha * Gamma1_["xy"] * T2["ijax"] * H2["yQjS"];
    C2["iQaS"] -= alpha * Gamma1_["XY"] * T2["iJaX"] * H2["YQJS"];

    if (print_ > 2) {
        outfile->Printf("\n    Time for [H2, T2] -> C2 : %12.3f", timer.get());
    }
    dsrg_time_.add("222", timer.get());
}

std::vector<std::string> MASTER_DSRG::diag_one_labels() {
    std::vector<std::string> labels;
    for (const std::string& p : {acore_label_, aactv_label_, avirt_label_}) {
        labels.push_back(p + p);
    }
    for (const std::string& p : {bcore_label_, bactv_label_, bvirt_label_}) {
        labels.push_back(p + p);
    }
    return labels;
}

std::vector<std::string> MASTER_DSRG::od_one_labels_hp() {
    std::vector<std::string> labels;
    for (const std::string& p : {acore_label_, aactv_label_}) {
        for (const std::string& q : {aactv_label_, avirt_label_}) {
            if (p == aactv_label_ && q == aactv_label_) {
                continue;
            }
            labels.push_back(p + q);
        }
    }

    for (const std::string& p : {bcore_label_, bactv_label_}) {
        for (const std::string& q : {bactv_label_, bvirt_label_}) {
            if (p == bactv_label_ && q == bactv_label_) {
                continue;
            }
            labels.push_back(p + q);
        }
    }
    return labels;
}

std::vector<std::string> MASTER_DSRG::od_one_labels_ph() {
    std::vector<std::string> blocks1(od_one_labels_hp());
    for (auto& block : blocks1) {
        std::swap(block[0], block[1]);
    }
    return blocks1;
}

std::vector<std::string> MASTER_DSRG::od_one_labels() {
    std::vector<std::string> labels(od_one_labels_hp());
    std::vector<std::string> temp(od_one_labels_ph());
    labels.insert(std::end(labels), std::begin(temp), std::end(temp));
    return labels;
}

std::vector<std::string> MASTER_DSRG::od_two_labels_hhpp() {
    std::map<char, std::vector<std::string>> space_map;
    space_map['h'] = {acore_label_, aactv_label_};
    space_map['H'] = {bcore_label_, bactv_label_};
    space_map['p'] = {aactv_label_, avirt_label_};
    space_map['P'] = {bactv_label_, bvirt_label_};

    auto hhpp_spin = [&](const std::string& block, const std::string& actv) {
        std::vector<std::string> out;
        for (const std::string& p : space_map[block[0]]) {
            for (const std::string& q : space_map[block[1]]) {
                for (const std::string& r : space_map[block[2]]) {
                    for (const std::string& s : space_map[block[3]]) {
                        out.push_back(p + q + r + s);
                    }
                }
            }
        }
        out.erase(std::remove(out.begin(), out.end(), actv), out.end());
        return out;
    };

    std::string aaaa = aactv_label_ + aactv_label_ + aactv_label_ + aactv_label_;
    std::string aAaA = aactv_label_ + bactv_label_ + aactv_label_ + bactv_label_;
    std::string AAAA = bactv_label_ + bactv_label_ + bactv_label_ + bactv_label_;

    std::vector<std::string> labels(hhpp_spin("hhpp", aaaa));
    std::vector<std::string> labels_ab(hhpp_spin("hHpP", aAaA));
    std::vector<std::string> labels_bb(hhpp_spin("HHPP", AAAA));

    labels.insert(std::end(labels), std::begin(labels_ab), std::end(labels_ab));
    labels.insert(std::end(labels), std::begin(labels_bb), std::end(labels_bb));

    return labels;
}

std::vector<std::string> MASTER_DSRG::od_two_labels_pphh() {
    std::vector<std::string> labels(od_two_labels_hhpp());
    for (auto& block : labels) {
        std::swap(block[0], block[2]);
        std::swap(block[1], block[3]);
    }
    return labels;
}

std::vector<std::string> MASTER_DSRG::od_two_labels() {
    std::vector<std::string> labels(od_two_labels_hhpp());
    std::vector<std::string> temp(od_two_labels_pphh());
    labels.insert(std::end(labels), std::begin(temp), std::end(temp));
    return labels;
}

std::vector<std::string> MASTER_DSRG::diag_two_labels() {
    std::map<char, std::vector<std::string>> general;
    general['a'] = {acore_label_, aactv_label_, avirt_label_};
    general['b'] = {bcore_label_, bactv_label_, bvirt_label_};

    auto all_spin = [&](const std::string& block) {
        std::vector<std::string> out;
        for (const std::string& p : general[block[0]]) {
            for (const std::string& q : general[block[1]]) {
                for (const std::string& r : general[block[2]]) {
                    for (const std::string& s : general[block[3]]) {
                        out.push_back(p + q + r + s);
                    }
                }
            }
        }
        return out;
    };

    std::vector<std::string> all(all_spin("aaaa"));
    std::vector<std::string> all_ab(all_spin("abab"));
    std::vector<std::string> all_bb(all_spin("bbbb"));

    all.insert(std::end(all), std::begin(all_ab), std::end(all_ab));
    all.insert(std::end(all), std::begin(all_bb), std::end(all_bb));

    std::vector<std::string> od(od_two_labels());
    std::sort(od.begin(), od.end());
    std::sort(all.begin(), all.end());

    std::vector<std::string> labels;
    std::set_symmetric_difference(all.begin(), all.end(), od.begin(), od.end(),
                                  std::back_inserter(labels));

    return labels;
}

std::vector<std::string> MASTER_DSRG::re_two_labels() {
    std::map<char, std::string> core, actv, virt;
    core['a'] = acore_label_;
    core['b'] = bcore_label_;
    actv['a'] = aactv_label_;
    actv['b'] = bactv_label_;
    virt['a'] = avirt_label_;
    virt['b'] = bvirt_label_;

    auto half_spin = [&](const std::string& spin) {
        const char& p = spin[0];
        const char& q = spin[1];
        std::vector<std::vector<std::string>> out{{core[p] + core[q]},
                                                  {actv[p] + actv[q]},
                                                  {virt[p] + virt[q]},
                                                  {core[p] + actv[q], actv[p] + core[q]},
                                                  {core[p] + virt[q], virt[p] + core[q]},
                                                  {actv[p] + virt[q], virt[p] + actv[q]}};
        return out;
    };
    std::vector<std::vector<std::string>> half_aa = half_spin("aa");
    std::vector<std::vector<std::string>> half_ab = half_spin("ab");
    std::vector<std::vector<std::string>> half_bb = half_spin("bb");

    std::vector<std::string> labels;
    for (const auto& halfs : {half_aa, half_ab, half_bb}) {
        for (const auto& half : halfs) {
            for (const std::string& half1 : half) {
                for (const std::string& half2 : half) {
                    labels.emplace_back(half1 + half2);
                }
            }
        }
    }

    return labels;
}
}
}
