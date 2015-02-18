/*
 *@BEGIN LICENSE
 *
 * Libadaptive: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

#ifndef _dsrg_mrpt2_h_
#define _dsrg_mrpt2_h_

#include <fstream>

#include <liboptions/liboptions.h>
#include <libmints/wavefunction.h>

#include "integrals.h"
#include "tensor_basic.h"
#include "tensor_labeled.h"
#include "tensor_product.h"
#include "tensor_blocked.h"
#include "reference.h"

namespace psi{

//class PSIO;
//class Chkpt;

namespace libadaptive{

/**
 * @brief The MethodBase class
 * This class provides basic functions to write electronic structure
 * pilot codes using the Tensor classes
 */
class DSRG_MRPT2 : public Wavefunction
{
protected:

    // => Class data <= //

    /// The reference object
    Reference reference_;

    /// The molecular integrals required by MethodBase
    ExplorerIntegrals* ints_;

    /// The number of correlated orbitals per irrep (excluding frozen core and virtuals)
    Dimension ncmopi_;
    /// The number of restricted doubly occupied orbitals per irrep (core)
    Dimension rdoccpi_;
    /// The number of active orbitals per irrep (active)
    Dimension actvpi_;
    /// The number of restricted unoccupied orbitals per irrep (virtual)
    Dimension ruoccpi_;

    /// List of alpha core MOs
    std::vector<size_t> acore_mos;
    /// List of alpha active MOs
    std::vector<size_t> aactv_mos;
    /// List of alpha virtual MOs
    std::vector<size_t> avirt_mos;

    /// List of beta core MOs
    std::vector<size_t> bcore_mos;
    /// List of beta active MOs
    std::vector<size_t> bactv_mos;
    /// List of beta virtual MOs
    std::vector<size_t> bvirt_mos;

    /// Map from all the MOs to the alpha core
    std::map<size_t,size_t> mos_to_acore;
    /// Map from all the MOs to the alpha active
    std::map<size_t,size_t> mos_to_aactv;
    /// Map from all the MOs to the alpha virtual
    std::map<size_t,size_t> mos_to_avirt;

    /// Map from all the MOs to the beta core
    std::map<size_t,size_t> mos_to_bcore;
    /// Map from all the MOs to the beta active
    std::map<size_t,size_t> mos_to_bactv;
    /// Map from all the MOs to the beta virtual
    std::map<size_t,size_t> mos_to_bvirt;

    /// The flow parameter
    double s_;

    /// Threshold for the Taylor expansion of f(z) = (1-exp(-z^2))/z
    double taylor_threshold_;
    /// Order of the Taylor expansion of f(z) = (1-exp(-z^2))/z
    int taylor_order_;

    // => Tensors <= //

    BlockedTensor H;
    BlockedTensor F;
    BlockedTensor V;
    BlockedTensor DFL;
    BlockedTensor Gamma1;
    BlockedTensor Eta1;
    BlockedTensor Lambda2;
    BlockedTensor Lambda3;
    BlockedTensor Delta1;
    BlockedTensor Delta2;

    // => Class initialization and termination <= //

    /// Called in the constructor
    void startup();
    /// Called in the destructor
    void cleanup();
    /// Print a summary of the options
    void print_summary();
public:

    // => Constructors <= //

    DSRG_MRPT2(Reference reference,boost::shared_ptr<Wavefunction> wfn, Options &options, ExplorerIntegrals* ints);

    ~DSRG_MRPT2();

    /// Compute the DSRG-MRPT2 energy
    double compute_energy();

    /// The energy of the reference
    double E0_;

    /// The frozen-core energy
    double frozen_core_energy_;
};

}} // End Namespaces

#endif // _dsrg_mrpt2_h_