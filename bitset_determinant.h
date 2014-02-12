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

#ifndef _bitset_determinant_h_
#define _bitset_determinant_h_

#include "integrals.h"
#include "excitation_determinant.h"
#include "boost/dynamic_bitset.hpp"

namespace psi{ namespace libadaptive{

/**
 * A class to store a Slater determinant using Boost's dynamic_bitset.
 *
 * The determinant is represented by a pair of alpha/beta strings
 * that specify the occupation of each molecular orbital
 * (including frozen core and virtual orbitals).
 *
 * |Det> = |Ia> x |Ib>
 *
 * The strings are represented using an array of bits, and the
 * following convention is used here:
 * true <-> 1
 * false <-> 0
 */
class BitsetDeterminant{
public:
    // Class Constructor and Destructor
    /// Construct an empty determinant
    BitsetDeterminant();
    /// Construct the determinant from an occupation vector that
    /// specifies the alpha and beta strings.  occupation = [Ia,Ib]
//    explicit BitsetDeterminant(const std::vector<int>& occupation,bool print_det = false);
    /// Construct the determinant from an occupation vector that
    /// specifies the alpha and beta strings.  occupation = [Ia,Ib]
//    explicit BitsetDeterminant(const std::vector<bool>& occupation,bool print_det = false);
    /// Construct an excited determinant of a given reference
    /// Construct the determinant from two occupation vectors that
    /// specifies the alpha and beta strings.  occupation = [Ia,Ib]
    explicit BitsetDeterminant(const std::vector<bool>& occupation_a,const std::vector<bool>& occupation_b,bool print_det = false);
//    /// Returns the number of non-frozen molecular orbitals
//    int nmo() {return nmo_;}
//    /// Get a pointer to the alpha bits
//    boost::dynamic_bitset<>& get_alfa_bits() {return alfa_bits_;}
//    /// Get a pointer to the beta bits
//    boost::dynamic_bitset<>& get_beta_bits() {return beta_bits_;}
    /// Return the value of an alpha bit
    bool get_alfa_bit(int n) const {return alfa_bits_[n];}
    /// Return the value of a beta bit
    bool get_beta_bit(int n) const {return beta_bits_[n];}
    /// Set the value of an alpha bit
    void set_alfa_bit(int n, bool value) {alfa_bits_[n] = value;}
    /// Set the value of a beta bit
    void set_beta_bit(int n, bool value) {beta_bits_[n] = value;}
//    /// Specify the occupation numbers
//    void set_bits(bool*& alfa_bits,bool*& beta_bits);
//    /// Specify the occupation numbers
//    void set_bits(std::vector<bool>& alfa_bits,std::vector<bool>& beta_bits);
    /// Print the Slater determinant
    void print() const;
    /// Compute the energy of a Slater determinant
    double energy() const;
//    /// Compute the one-electron contribution to the energy of a Slater determinant
//    double one_electron_energy();
//    /// Compute the kinetic energy of a Slater determinant
//    double kinetic_energy();
//    /// Compute the energy of a Slater determinant with respect to a given reference
//    double excitation_energy(const StringDeterminant& reference);
//    /// Compute the energy of a Slater determinant with respect to a given reference
//    double excitation_ab_energy(const StringDeterminant& reference);
    /// Compute the matrix element of the Hamiltonian between this determinant and a given one
    double slater_rules(const BitsetDeterminant& rhs) const;
//    /// Compute the excitation level of a Slater determiant with respect to a given reference
//    int excitation_level(const StringDeterminant& reference);
//    int excitation_level(const bool* Ia,const bool* Ib);
    /// Sets the pointer to the integral object
    static void set_ints(ExplorerIntegrals* ints) {
        ints_ = ints;
        temp_alfa_bits_.resize(ints_->nmo());
        temp_beta_bits_.resize(ints_->nmo());
    }

    bool operator<(const BitsetDeterminant& lhs) const{
        if (alfa_bits_ > lhs.alfa_bits_) return false;
        if (alfa_bits_ < lhs.alfa_bits_) return true;
        return beta_bits_ < lhs.beta_bits_;
    }

    bool operator==(const BitsetDeterminant& lhs) const{
        return ((alfa_bits_ == lhs.alfa_bits_) and (beta_bits_ == lhs.beta_bits_));
    }


private:
    // Functions
    /// Used to allocate the memory for the arrays
    void allocate();

    // Data
    /// Number of non-frozen molecular orbitals
    size_t nmo_;
    /// The occupation vector for the alpha electrons (does not include the frozen orbitals)
    boost::dynamic_bitset<> alfa_bits_;
    /// The occupation vector for the beta electrons (does not include the frozen orbitals)
    boost::dynamic_bitset<> beta_bits_;

    // Static data
    /// A pointer to the integral object
    static ExplorerIntegrals* ints_;
    static boost::dynamic_bitset<> temp_alfa_bits_;
    static boost::dynamic_bitset<> temp_beta_bits_;
};

}} // End Namespaces

#endif // _bitset_determinant_h_
