/***************************************************************************
 *
 * Author: "Sjors H.W. Scheres"
 * MRC Laboratory of Molecular Biology
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
 * This complete copyright notice must be included in any revised version of the
 * source code. Additional authorship citations may be added, but existing
 * author citations must be preserved.
 ***************************************************************************/
/***************************************************************************
 *
 * Authors: Carlos Oscar S. Sorzano (coss@cnb.csic.es)
 *
 * Unidad de Bioinformatica of Centro Nacional de Biotecnologia , CSIC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 e You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 *
 * All comments concerning this program package may be sent to the
 * e-mail address 'xmipp@cnb.csic.es'
 ***************************************************************************/

#ifndef _CTF_HH
#define _CTF_HH

#include "src/multidim_array.h"
#include "src/metadata_table.h"
#include "src/jaz/obs_model.h"
#include <map>


class CTF {

    protected:

    // Different constants
    RFLOAT K1, K2, K3, K4, K5;

    // Astigmatism stored in symmetrical matrix form
    RFLOAT Axx, Axy, Ayy;

    // Azimuthal angle (radians)
    RFLOAT rad_azimuth;

    // defocus_average = (defocus_u + defocus_v)/2
    RFLOAT defocus_average;

    // defocus_deviation = (defocus_u - defocus_v)/2
    RFLOAT defocus_deviation;

    // Pointer to observation model kept after a call to readByGroup() to enable
    // caching of symmetric aberrations (CTF instances can be reallocated for each particle,
    // while the same obs. model lives for the entire duration of the program)
    ObservationModel *obsModel;
    int opticsGroup;

    public:

    // Acceleration voltage (kilovolts)
    RFLOAT kV;

    // Defocus in U (in Angstroms).
    // Positive values are underfocused.
    RFLOAT DeltafU;

    // Defocus in V (in Angstroms).
    // Positive values are underfocused.
    RFLOAT DeltafV;

    // Azimuthal angle (between X and U) in degrees
    RFLOAT azimuthal_angle;

    // Electron wavelength (Angstroms)
    RFLOAT lambda;

    // Radius of the aperture (in micras)
    // RFLOAT aperture;

    // Spherical aberration (in millimeters).
    // Typical value 5.6
    RFLOAT Cs;

    // Chromatic aberration (in millimeters).
    // Typical value 2
    RFLOAT Ca;

    // Mean energy loss (in eV) due to interaction with sample.
    // Typical value 1
    RFLOAT espr;

    // Objective lens stability (deltaI/I) (ppm).
    // Typical value 1
    RFLOAT ispr;

    // Convergence cone semiangle (in mrad).
    // Typical value 0.5
    RFLOAT alpha;

    // Longitudinal mechanical displacement (Angstrom). Typical value 100
    RFLOAT DeltaF;

    // Transversal mechanical displacement (Angstrom). Typical value 3
    RFLOAT DeltaR;

    // Amplitude contrast. Typical values 0.07 for cryo, 0.2 for negative stain
    RFLOAT Q0;

    // B-factor fall-off
    RFLOAT Bfac;

    // Overall scale-factor of CTF
    RFLOAT scale;

    // Phase-shift from a phase-plate (in rad)
    RFLOAT phase_shift;

    /** Empty constructor. */
    CTF():
    kV(200), DeltafU(0), DeltafV(0), azimuthal_angle(0), phase_shift(0),
    Cs(0), Bfac(0), Q0(0), scale(1), obsModel(0), opticsGroup(0)
    {}

    CTF(
        RFLOAT _defU, RFLOAT _defV, RFLOAT _defAng,
        RFLOAT _voltage, RFLOAT _Cs, RFLOAT _Q0,
        RFLOAT _Bfac = 0.0, RFLOAT _scale = 1.0, RFLOAT _phase_shift = 0.0
    ) {
        setValues(_defU, _defV, _defAng, _voltage, _Cs, _Q0, _Bfac, _scale, _phase_shift);
    }

    CTF(
        ObservationModel *obs, int opticsGroup,
        RFLOAT _defU, RFLOAT _defV, RFLOAT _defAng,
        RFLOAT _Bfac = 0.0, RFLOAT _scale = 1.0, RFLOAT _phase_shift = 0.0
    ) {
        setValuesByGroup(
            obs, opticsGroup,
            _defU, _defV, _defAng,
            _Bfac, _scale, _phase_shift
        );
    }

    CTF(
        const MetaDataTable &partMdt, ObservationModel *obs,
        long int particle = -1
    ) {
        readByGroup(partMdt, obs, particle);
    }

    CTF(
        const MetaDataTable &MD1, const MetaDataTable &MD2,
        long int objectID = -1
    ) {
        read(MD1, MD2, objectID);
    }

    // Read CTF parameters from particle table partMdt and optics table opticsMdt.
    void readByGroup(
        const MetaDataTable &partMdt, ObservationModel *obs,
        long int particle = -1
    );

    void readValue(
        EMDL::EMDLabel label, RFLOAT &dest, RFLOAT defaultVal,
        long int particle, int opticsGroup,
        const MetaDataTable &partMdt, const ObservationModel *obs
    );

    /** Read CTF parameters from MetaDataTables MD1 and MD2 (deprecated).
    * If a parameter is not found in MD1 it is tried to be read from MD2.
    * If it is also not found in the second then a default value is used.
    * This is useful if micrograph-specific parameters are stored in a separate MD from the image-specific parameters.
    */
    void read(
        const MetaDataTable &MD1, const MetaDataTable &MD2,
        long int objectID = -1
    );

    /** Just set all values explicitly */
    void setValues(
        RFLOAT _defU, RFLOAT _defV, RFLOAT _defAng,
        RFLOAT _voltage, RFLOAT _Cs, RFLOAT _Q0,
        RFLOAT _Bfac, RFLOAT _scale = 1.0, RFLOAT _phase_shift = 0.0
    );

    /** Set all values explicitly in 3.1 */
    void setValuesByGroup(
        ObservationModel *obs, int opticsGroup,
        RFLOAT _defU, RFLOAT _defV, RFLOAT _defAng,
        RFLOAT _Bfac = 0.0, RFLOAT _scale = 1.0, RFLOAT _phase_shift = 0.0
    );

    /** Read from a single MetaDataTable */
    void read(const MetaDataTable &MD);

    /** Write to MetaDataTable. */
    void write(MetaDataTable &MD);

    /** Write to output. */
    void write(std::ostream &out);

    // Set up the CTF object, read parameters from MetaDataTables with micrograph and particle information
    // If no MDmic is provided or it does not contain certain parameters, these parameters are tried to be read from MDimg
    void initialise();

    RFLOAT operator () (RFLOAT X, RFLOAT Y) {
        return getCTF(X, Y);
    }

    // Compute CTF at (U,V). Continuous frequencies
    inline RFLOAT getCTF(
        RFLOAT X, RFLOAT Y,
        bool do_only_flip_phases = false,
        bool do_intact_until_first_peak = false, bool do_damping = true,
        double gammaOffset = 0.0, bool do_intact_after_first_peak = false
    ) const {
        if (obsModel && obsModel->hasMagMatrices) {
            const Matrix2D<RFLOAT> &M = obsModel->getMagMatrix(opticsGroup);
            RFLOAT Xd = M(0, 0) * X + M(0, 1) * Y;
            RFLOAT Yd = M(1, 0) * X + M(1, 1) * Y;

            X = Xd;
            Y = Yd;
        }

        RFLOAT u2 = X * X + Y * Y;  // u2 is the squared hypotenuse length of a right triangle with side lengths X, Y
        RFLOAT u4 = u2 * u2;

        // if (u2>=ua2) return 0;
        // RFLOAT deltaf = getDeltaF(X, Y);
        // RFLOAT gamma = K1 * deltaf * u2 + K2 * u4 - K5 - K3 + gammaOffset;
        RFLOAT gamma = K1 * (Axx * X * X + 2.0 * Axy * X * Y + Ayy * Y * Y) + K2 * u4 - K5 - K3 + gammaOffset;
        // Quadratic: xx + 2xy + yy

        RFLOAT retval = (
            do_intact_until_first_peak && abs(gamma) < PI / 2.0 ||
            do_intact_after_first_peak && abs(gamma) > PI / 2.0
        ) ? 1.0 : -sin(gamma);

        if (do_damping) {
            RFLOAT E = exp(K4 * u2); // B-factor decay (K4 = -Bfac/4);
            retval *= E;
        }

        if (do_only_flip_phases) {
            retval = retval == 0.0 ? 1.0 : sgn(retval);
        }

        retval *= scale;

        // SHWS 25-2-2019: testing a new idea to improve code stability
        // In order to prevent division by zero in GPU code, 
        // don't allow very small CTF values.
        if (fabs(retval) < 1e-8) {
            retval = 1e-8 * (retval == 0.0 ? 1.0 : sgn(retval));
        }

        return retval;
    }

    RFLOAT getGamma(RFLOAT X, RFLOAT Y) const;

    // compute the local frequency of the ctf
    // (i.e. the radial slope of 'double gamma' in getCTF())
    // -- deprecated, use getGammaGrad().length()
    RFLOAT getCtfFreq(RFLOAT X, RFLOAT Y);

    gravis::t2Vector<RFLOAT> getGammaGrad(RFLOAT X, RFLOAT Y) const;

    inline Complex getCTFP(RFLOAT X, RFLOAT Y, bool is_positive, double gammaOffset = 0.0) const {
        if (obsModel && obsModel->hasMagMatrices) {
            const Matrix2D<RFLOAT> &M = obsModel->getMagMatrix(opticsGroup);
            RFLOAT Xd = M(0, 0) * X + M(0, 1) * Y;
            RFLOAT Yd = M(1, 0) * X + M(1, 1) * Y;

            X = Xd;
            Y = Yd;
        }

        RFLOAT u2 = X * X + Y * Y;
        RFLOAT u4 = u2 * u2;

        RFLOAT gamma = K1 * (Axx * X * X + 2.0 * Axy * X * Y + Ayy * Y * Y) + K2 * u4 - K5 - K3 + gammaOffset + PI / 2.0;

        RFLOAT sinx, cosx;
        #ifdef RELION_SINGLE_PRECISION
            SINCOSF(gamma, &sinx, &cosx);
        #else
            SINCOS(gamma, &sinx, &cosx);
        #endif

        return { cosx, is_positive ? sinx : -sinx };
    }

    // Compute Deltaf at a given direction (no longer used by getCTF)
    inline RFLOAT getDeltaF(RFLOAT X, RFLOAT Y) const {
        if (abs(X) < XMIPP_EQUAL_ACCURACY && abs(Y) < XMIPP_EQUAL_ACCURACY)
            return 0;

        RFLOAT ellipsoid_ang = atan2(Y, X) - rad_azimuth;
        /*
        * For a derivation of this formula,
        * see Principles of Electron Optics p. 1380.
        * In particular, term defocus and twofold axial astigmatism
        * take into account that a1 and a2 are
        * the coefficient of the Zernike polynomials difference of defocus
        * at 0 and at 45 degrees.
        * In this case, a2 = 0.
        */
        return defocus_average + defocus_deviation * cos(2 * ellipsoid_ang);

    }

    // Generate (Fourier-space, i.e. FFTW format) image with all CTF values.
    // The dimensions of the result array should have been set correctly already
    void getFftwImage(
        MultidimArray<RFLOAT> &result, int orixdim, int oriydim, RFLOAT angpix,
        bool do_abs = false, bool do_only_flip_phases = false,
        bool do_intact_until_first_peak = false, bool do_damping = true,
        bool do_ctf_padding = false, bool do_intact_after_first_peak = false
    ) const;

    // Get a complex image with the CTFP/Q values, where the angle is in degrees between the Y-axis and the CTFP/Q sector line
    void getCTFPImage(
        MultidimArray<Complex> &result, int orixdim, int oriydim, RFLOAT angpix,
        bool is_positive, float angle
    );

    // Generate a centered image (with Hermitian symmetry)
    // The dimensions of the result array should have been set correctly already
    void getCenteredImage(
        MultidimArray<RFLOAT> &result, RFLOAT angpix,
        bool do_abs = false, bool do_only_flip_phases = false,
        bool do_intact_until_first_peak = false, bool do_damping = true,
        bool do_intact_after_first_peak = false
    );

    // Generate a 1D profile along defocusAngle
    // The dimensions of the result array should have been set correctly already, i.e. at the image size!
    void get1DProfile(
        MultidimArray<RFLOAT> &result, RFLOAT angle, RFLOAT angpix,
        bool do_abs = false, bool do_only_flip_phases = false,
        bool do_intact_until_first_peak = false, bool do_damping = true,
        bool do_intact_after_first_peak = false
    );

    // Calculate weight W for Ewald-sphere curvature correction: apply this to the result from getFftwImage
    void applyWeightEwaldSphereCurvature(
        MultidimArray<RFLOAT> &result, int orixdim, int oriydim, RFLOAT angpix,
        RFLOAT particle_diameter
    );

    void applyWeightEwaldSphereCurvature_new(
        MultidimArray<RFLOAT> &result, int orixdim, int oriydim, RFLOAT angpix,
        RFLOAT particle_diameter
    );

    // Calculate weight W for Ewald-sphere curvature correction: apply this to the result from getFftwImage
    void applyWeightEwaldSphereCurvature_noAniso(
        MultidimArray<RFLOAT> &result, int orixdim, int oriydim, RFLOAT angpix,
        RFLOAT particle_diameter
    );

    std::vector<double> getK();
    double getAxx();
    double getAxy();
    double getAyy();

};

#endif
