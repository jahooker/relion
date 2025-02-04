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
#include "src/autopicker.h"
#include "src/time.h"
#include "src/fftw.h"
#include "src/mask.h"
#include "src/helix.h"
#include "src/jaz/ctf_helper.h"
#include "src/multidim_array_statistics.h"
#include "src/plot_metadata.h"
#include <memory>  // std::unique_ptr

// #define DEBUG
// #define DEBUG_HELIX

std::ostream &operator << (std::ostream &os, AmyloidCoord coord) {
   os << "{ x = " << coord.x << ", y = " << coord.y;
   os << ", psi = " << coord.psi << ", fom = " << coord.fom << " }";
   return os;
}

inline RFLOAT safer_atan2(RFLOAT dy, RFLOAT dx) {
    return fabs(dx) < 0.01 && fabs(dy) < 0.01 ? 0.0 : atan2(dy, dx);
}

// Search for this micrograph in the metadata table
CTF find_micrograph_ctf(MetaDataTable &mdt, const FileName &fn_mic, ObservationModel &obsModel) {
    for (long int i : mdt) {  // BEWARE: Iterating over mdt modifies it.
        if (fn_mic == mdt.getValue<std::string>(EMDL::MICROGRAPH_NAME, i)) {
            return CtfHelper::makeCTF(mdt, &obsModel);
        }
    }
    REPORT_ERROR("Logic error: failed to find CTF information for " + fn_mic);
}

// Squared distance between two peaks
inline int dist2(const Peak peak1, const Peak peak2) {
    const int dx = peak1.x - peak2.x;
    const int dy = peak1.y - peak2.y;
    return hypot2(dx, dy);
}

void ccfPeak::clear() {
    id = ref = nr_peak_pixel = -1;
    x = y = r = area_percentage = fom_max = psi = dist = fom_thres = -1.0;
    ccf_pixel_list.clear();
}

bool ccfPeak::isValid() const {
    // Invalid parameters
    if (r < 0.0 || area_percentage < 0.0 || ccf_pixel_list.empty())
        return false;
    /// TODO: check ccf values in ccf_pixel_list?
    return std::any_of(ccf_pixel_list.begin(), ccf_pixel_list.end(),
        [this] (const ccfPixel& pixel) { return pixel.fom > fom_thres; });
}

bool ccfPeak::operator < (const ccfPeak& b) const {
    if (fabs(r - b.r) < 0.01)
        return fom_max < b.fom_max;
    return r < b.r;
}

bool ccfPeak::refresh() {

    area_percentage = -1.0;

    if (ccf_pixel_list.empty()) return false;

    fom_max = -99.0e99;
    int nr_valid_pixel = 0;
    RFLOAT x_avg = 0.0, y_avg = 0.0;
    for (const ccfPixel &pixel : ccf_pixel_list) {
        if (pixel.fom > fom_thres) {
            nr_valid_pixel++;

            if (pixel.fom > fom_max) { fom_max = pixel.fom; }

            x_avg += pixel.x;
            y_avg += pixel.y;
        }
    }
    nr_peak_pixel = nr_valid_pixel;

    if (nr_valid_pixel < 1) return false;

    x = x_avg / (RFLOAT) nr_valid_pixel;
    y = y_avg / (RFLOAT) nr_valid_pixel;
    area_percentage = (RFLOAT) nr_valid_pixel / ccf_pixel_list.size();

    return true;
};

void AutoPicker::read(int argc, char **argv) {
    parser.setCommandLine(argc, argv);

    int gen_section = parser.addSection("General options");
    fn_in = parser.getOption("--i", "Micrograph STAR file OR filenames from which to autopick particles, e.g. \"Micrographs/*.mrc\"");
    fn_out = parser.getOption("--pickname", "Rootname for coordinate STAR files", "autopick");
    fn_odir = parser.getOption("--odir", "Output directory for coordinate files (default is to store next to micrographs)", "AutoPick/");
    angpix = textToFloat(parser.getOption("--angpix", "Pixel size of the micrographs in Angstroms", "1"));
    particle_diameter = textToFloat(parser.getOption("--particle_diameter", "Diameter of the circular mask that will be applied to the experimental images (in Angstroms, default=automatic)", "-1"));
    decrease_radius = textToInteger(parser.getOption("--shrink_particle_mask", "Shrink the particle mask by this many pixels (to detect Einstein-from-noise classes)", "2"));
    outlier_removal_zscore= textToFloat(parser.getOption("--outlier_removal_zscore", "Remove pixels that are this many sigma away from the mean", "8."));
    do_write_fom_maps = parser.checkOption("--write_fom_maps", "Write calculated probability-ratio maps to disc (for re-reading in subsequent runs)");
    no_fom_limit = parser.checkOption("--no_fom_limit", "Ignore default maximum limit of 30 fom maps being written","false");
    do_read_fom_maps = parser.checkOption("--read_fom_maps", "Skip probability calculations, re-read precalculated maps from disc");
    do_optimise_scale = !parser.checkOption("--skip_optimise_scale", "Skip the optimisation of the micrograph scale for better prime factors in the FFTs. This runs slower, but at exactly the requested resolution.");
    do_only_unfinished = parser.checkOption("--only_do_unfinished", "Only autopick those micrographs for which the coordinate file does not yet exist");
    do_gpu = parser.checkOption("--gpu", "Use GPU acceleration when availiable");
    gpu_ids = parser.getOption("--gpu", "Device ids for each MPI-thread","default");
    #ifndef CUDA
    if (do_gpu) {
        std::cerr << "+ WARNING : Relion was compiled without CUDA of at least version 7.0 - you do NOT have support for GPUs" << std::endl;
        do_gpu = false;
    }
    #endif
    int ref_section = parser.addSection("References options");
    fn_ref = parser.getOption("--ref", "STAR file with the reference names, or an MRC stack with all references, or \"gauss\" for blob-picking","");
    angpix_ref = textToFloat(parser.getOption("--angpix_ref", "Pixel size of the references in Angstroms (default is same as micrographs)", "-1"));
    do_invert = parser.checkOption("--invert", "Density in micrograph is inverted w.r.t. density in template");
    psi_sampling = textToFloat(parser.getOption("--ang", "Angular sampling (in degrees); use 360 for no rotations", "10"));
    lowpass = textToFloat(parser.getOption("--lowpass", "Lowpass filter in Angstroms for the references (prevent Einstein-from-noise!)","-1"));
    highpass = textToFloat(parser.getOption("--highpass", "Highpass filter in Angstroms for the micrographs","-1"));
    do_ctf = parser.checkOption("--ctf", "Perform CTF correction on the references?");
    intact_ctf_first_peak = parser.checkOption("--ctf_intact_first_peak", "Ignore CTFs until their first peak?");
    gauss_max_value = textToFloat(parser.getOption("--gauss_max", "Value of the peak in the Gaussian blob reference","0.1"));
    healpix_order = textToInteger(parser.getOption("--healpix_order", "Healpix order for projecting a 3D reference (hp0=60deg; hp1=30deg; hp2=15deg)", "1"));
    symmetry = parser.getOption("--sym", "Symmetry point group for a 3D reference","C1");

    int log_section = parser.addSection("Laplacian-of-Gaussian options");
    do_LoG = parser.checkOption("--LoG", "Use Laplacian-of-Gaussian filter-based picking, instead of template matching");
    LoG_min_diameter = textToFloat(parser.getOption("--LoG_diam_min", "Smallest particle diameter (in Angstroms) for blob-detection by Laplacian-of-Gaussian filter", "-1"));
    LoG_max_diameter = textToFloat(parser.getOption("--LoG_diam_max", "Largest particle diameter (in Angstroms) for blob-detection by Laplacian-of-Gaussian filter", "-1"));
    LoG_neighbour_fudge = textToFloat(parser.getOption("--LoG_neighbour", "Avoid neighbouring particles within (the detected diameter + the minimum diameter) times this percent", "100"));
    LoG_neighbour_fudge /= 100.0;
    LoG_invert = parser.checkOption("--Log_invert", "Use this option if the particles are white instead of black");
    LoG_adjust_threshold = textToFloat(parser.getOption("--LoG_adjust_threshold", "Use this option to adjust the picking threshold: positive for less particles, negative for more", "0."));
    LoG_upper_limit = textToFloat(parser.getOption("--LoG_upper_threshold", "Use this option to set the upper limit of the picking threshold", "99999"));
    LoG_use_ctf = parser.checkOption("--LoG_use_ctf", "Use CTF until the first peak in Laplacian-of-Gaussian picker");

    if (do_gpu && do_LoG) {
        REPORT_ERROR("The Laplacian-of-Gaussian picker does not support GPU acceleration. Please remove --gpu option.");
    }

    int helix_section = parser.addSection("Helix options");
    autopick_helical_segments = parser.checkOption("--helix", "Are the references 2D helical segments? If so, in-plane rotation angles (psi) are estimated for the references.");
    helical_tube_curvature_factor_max = textToFloat(parser.getOption("--helical_tube_kappa_max", "Factor of maximum curvature relative to that of a circle", "0.25"));
    helical_tube_diameter = textToFloat(parser.getOption("--helical_tube_outer_diameter", "Tube diameter in Angstroms", "-1"));
    helical_tube_length_min = textToFloat(parser.getOption("--helical_tube_length_min", "Minimum tube length in Angstroms", "-1"));
    do_amyloid = parser.checkOption("--amyloid", "Activate specific algorithm for amyloid picking?");
    max_local_avg_diameter = textToFloat(parser.getOption("----max_diam_local_avg", "Maximum diameter to calculate local average density in Angstroms", "-1"));

    int peak_section = parser.addSection("Peak-search options");
    min_fraction_expected_Pratio = textToFloat(parser.getOption("--threshold", "Fraction of expected probability ratio in order to consider peaks?", "0.25"));
    min_particle_distance = textToFloat(parser.getOption("--min_distance", "Minimum distance (in A) between any two particles (default is half the box size)","-1"));
    max_stddev_noise = textToFloat(parser.getOption("--max_stddev_noise", "Maximum standard deviation in the noise area to use for picking peaks (default is no maximum)","-1"));
    min_avg_noise = textToFloat(parser.getOption("--min_avg_noise", "Minimum average in the noise area to use for picking peaks (default is no minimum)","-999."));
    autopick_skip_side = textToInteger(parser.getOption("--skip_side", "Keep this many extra pixels (apart from particle_size/2) away from the edge of the micrograph ","0"));

    int expert_section = parser.addSection("Expert options");
    verb = textToInteger(parser.getOption("--verb", "Verbosity", "1"));
    padding = textToInteger(parser.getOption("--pad", "Padding factor for Fourier transforms", "2"));
    random_seed = textToInteger(parser.getOption("--random_seed", "Number for the random seed generator", "1"));
    workFrac = textToFloat(parser.getOption("--shrink", "Reduce micrograph to this fraction size, during correlation calc (saves memory and time)", "1.0"));
    LoG_max_search = textToFloat(parser.getOption("--Log_max_search", "Maximum diameter in LoG-picking multi-scale approach is this many times the min/max diameter", "5."));
    extra_padding = textToInteger(parser.getOption("--extra_pad", "Number of pixels for additional padding of the original micrograph", "0"));

    // Check for errors in the command-line option
    if (parser.checkForErrors())
        REPORT_ERROR("Errors encountered on the command line (see above), exiting...");

    if (autopick_helical_segments) {
        if (helical_tube_curvature_factor_max < 0.0001 || helical_tube_curvature_factor_max > 1.0001)
            REPORT_ERROR("Error: Maximum curvature factor should be 0~1!");
        if (min_particle_distance <= 0.0)
            REPORT_ERROR("Error: Helical rise and the number of asymmetrical units between neighbouring helical segments should be positive!");
    }
}

void AutoPicker::usage() {
    parser.writeUsage(std::cout);
}

void AutoPicker::initialise() {
    #ifdef TIMING
    TIMING_A0 = timer.setNew("Initialise()");
    TIMING_A1 = timer.setNew("--Init");
    TIMING_A2 = timer.setNew("--Read Reference(s)");
    TIMING_A3 = timer.setNew("--Read Micrograph(s)");
    TIMING_A4 = timer.setNew("--Prep projectors");
    TIMING_A5 = timer.setNew("autoPickOneMicrograph()");
    TIMING_A6 = timer.setNew("--Read Micrographs(s)");
    TIMING_A7 = timer.setNew("--Micrograph computestats");
    TIMING_A8 = timer.setNew("--CTF-correct micrograph");
    TIMING_A9 = timer.setNew("--Resize CCF and PSI-maps");
    TIMING_B1 = timer.setNew("--FOM prep");
    TIMING_B2 = timer.setNew("--Read reference(s) via FOM");
    TIMING_B3 = timer.setNew("--Psi-dep correlation calc");
    TIMING_B4 = timer.setNew("----ctf-correction");
    TIMING_B5 = timer.setNew("----first psi");
    TIMING_B6 = timer.setNew("----rest of psis");
    TIMING_B7 = timer.setNew("----write fom maps");
    TIMING_B8 = timer.setNew("----peak-prune/-search");
    TIMING_B9 = timer.setNew("--final peak-prune");
    #endif

    {
    ifdefTIMING(TicToc tt (timer, TIMING_A0);)

    {
    ifdefTIMING(TicToc tt (timer, TIMING_A1);)

    if (random_seed == -1) { random_seed = time(nullptr); }

    if (fn_in.isStarFile()) {
        ObservationModel::loadSafely(fn_in, obsModel, MDmic, "micrographs", verb);
        fn_micrographs.clear();
        for (long int i : MDmic) {
            const FileName fn_mic = MDmic.getValue<std::string>(EMDL::MICROGRAPH_NAME, i);
            fn_micrographs.push_back(fn_mic);
        }

        // Check all optics groups have the same pixel size (check for same micrograph size is performed while running through all of them)
        if (!obsModel.opticsMdt.containsLabel(EMDL::MICROGRAPH_PIXEL_SIZE))
            REPORT_ERROR("The input does not contain the rlnMicrographPixelSize column.");
        angpix = obsModel.opticsMdt.getValue<RFLOAT>(EMDL::MICROGRAPH_PIXEL_SIZE, 0);
        for (int optics_group = 1; optics_group < obsModel.numberOfOpticsGroups(); optics_group++) {
            RFLOAT my_angpix = obsModel.opticsMdt.getValue<RFLOAT>(EMDL::MICROGRAPH_PIXEL_SIZE, optics_group);
            if (fabs(angpix - my_angpix) > 0.01) {
                REPORT_ERROR("ERROR: different pixel size for the different optics groups, perform autopicking separately per optics group.");
            }
        }
    } else {
        if (do_ctf)
            REPORT_ERROR("AutoPicker::initialise ERROR: use an input STAR file with the CTF information when using --ctf");

        fn_in.globFiles(fn_micrographs);
        if (fn_micrographs.empty())
            REPORT_ERROR("Cannot find any micrograph called: " + fns_autopick);
    }

    fn_ori_micrographs = fn_micrographs;
    // If we're continuing an old run, see which micrographs have not been finished yet...
    if (do_only_unfinished) {
        if (verb > 0)
            std::cout << " + Skipping those micrographs for which coordinate file already exists" << std::endl;
        fn_micrographs.erase(std::remove_if(
            fn_micrographs.begin(), fn_micrographs.end(), 
            [this] (const FileName &fn_mic) {
                return exists(getOutputRootName(fn_mic) + "_" + fn_out + ".star");
            }
        ), fn_micrographs.end());
    }

    // If there is nothing to do, return early
    todo_anything = !fn_micrographs.empty();
    if (!todo_anything) {
        if (verb > 0)
            std::cout << " + No new micrographs to do, so exiting autopicking ..." << std::endl;
        return;
    }

    if (verb > 0) {
        if (fn_micrographs.size() > 30 && do_write_fom_maps && !no_fom_limit) {
            REPORT_ERROR("\n If you really want to write this many (" + integerToString(fn_micrographs.size()) + ") FOM-maps, add --no_fom_limit");
        }
        std::cout << " + Run autopicking on the following micrographs: " << std::endl;
        for (const FileName &fn_micrograph : fn_micrographs)
            std::cout << "    * " << fn_micrograph << std::endl;
    }
    }

    {
    ifdefTIMING(TicToc tt (timer, TIMING_A2);)

    // Make sure that psi-sampling is even around the circle
    RFLOAT old_sampling = psi_sampling;
    int n_sampling = round(360.0 / psi_sampling);
    psi_sampling = 360.0 / (RFLOAT) n_sampling;
    if (verb > 0 && fabs(old_sampling - psi_sampling) > 1e-3)
        std::cout << " + Changed psi-sampling rate to: " << psi_sampling << std::endl;

    // Read in the references
    Mrefs.clear();
    if (do_LoG) {
        if (LoG_min_diameter < 0)
            REPORT_ERROR("ERROR: Provide --LoG_diam_min when using the LoG-filter for autopicking");
        if (LoG_max_diameter < 0)
            REPORT_ERROR("ERROR: Provide --LoG_diam_max when using the LoG-filter for autopicking");

        // Always use skip_side, as algorithms tends to pick on the sides of micrographs
        autopick_skip_side = std::max(autopick_skip_side, (int) (0.5 * LoG_min_diameter / angpix));

        // Fill vector with diameters to be searched
        diams_LoG.clear();
        for (int i = LoG_max_search; i > 1; i--)
            diams_LoG.push_back(round(LoG_min_diameter / (RFLOAT) i));
        diams_LoG.push_back(LoG_min_diameter);
        diams_LoG.push_back((LoG_max_diameter + LoG_min_diameter) / 2.0);
        diams_LoG.push_back(LoG_max_diameter);
        for (int i = 2; i <= LoG_max_search; i++)
            diams_LoG.push_back(round(LoG_max_diameter * (RFLOAT) i));

        if (verb > 0) {
            std::cout << " + Will use following diameters for Laplacian-of-Gaussian filter: " << std::endl;
            for (const RFLOAT &d : diams_LoG) {
                if (d < LoG_min_diameter) {
                    std::cout << "   * " << d << " (too low)" << std::endl;
                } else if (d > LoG_max_diameter) {
                    std::cout << "   * " << d << " (too high)" << std::endl;
                } else {
                    std::cout << "   * " << d << " (ok)" << std::endl;
                }
            }
        }

    } else if (fn_ref == "") {
        REPORT_ERROR("ERROR: Provide either --ref or use --LoG.");
    } else if (fn_ref == "gauss") {
        if (verb > 0)
            std::cout << " + Will use Gaussian blob as reference, with peak value of " << gauss_max_value << std::endl;

        if (particle_diameter <= 0)
            CRITICAL(ERR_GAUSSBLOBSIZE);

        // Set particle boxsize to be 1.5x bigger than circle with particle_diameter
        particle_size = 1.5 * round(particle_diameter / angpix);
        particle_size += particle_size % 2;
        psi_sampling = 360.0;
        do_ctf = false;

        auto ref = MultidimArray<RFLOAT>::zeros(particle_size, particle_size).setXmippOrigin();
        // Make a Gaussian reference. sigma is 1/6th of the particle size, such that 3 sigma is at the image edge
        RFLOAT normgauss = gaussian1D(0.0, particle_size / 6.0, 0.0);
        FOR_ALL_ELEMENTS_IN_ARRAY2D(ref, i, j) {
            double r = hypot(i, j);
            ref.elem(i, j) = gauss_max_value * gaussian1D(r, particle_size / 6.0, 0.0) / normgauss;
        }
        Mrefs.push_back(ref);

    } else if (fn_ref.isStarFile()) {
        MetaDataTable MDref;
        MDref.read(fn_ref);
        for (long int i : MDref) {
            // Get all reference images and their names
            Image<RFLOAT> Iref;

            FileName fn_img;
            try {
                fn_img = MDref.getValue<std::string>(EMDL::MLMODEL_REF_IMAGE, i);
            } catch (const char *errmsg) {
                try {
                    fn_img = MDref.getValue<std::string>(EMDL::IMAGE_NAME, i);
                } catch (const char* errmsg) {
                    REPORT_ERROR("AutoPicker::initialise ERROR: either provide rlnReferenceImage or rlnImageName in the reference STAR file!");
                }
            }

            #ifdef DEBUG
            std::cerr << " Reference fn= " << fn_img << std::endl;
            #endif
            Iref.read(fn_img);
            Iref().setXmippOrigin();
            Mrefs.push_back(Iref());

            if (Mrefs.size() == 1) {
                // For the first reference,
                // check pixel size in the header is consistent with angpix_ref.
                // Otherwise, raise a warning.
                RFLOAT angpix_header = Iref.samplingRateX();
                if (angpix_ref < 0) {
                    if (verb > 0 && fabs(angpix_header - angpix) > 1e-3) {
                        std::cout << " + Using pixel size in reference image header= " << angpix_header << std::endl;
                    }
                    angpix_ref = angpix_header;
                } else {
                    if (verb > 0 && fabs(angpix_header - angpix_ref) > 1e-3) {
                        std::cerr << " WARNING!!! Pixel size in reference image header= " << angpix_header << " but you have provided --angpix_ref " << angpix_ref << std::endl;
                    }
                }
            }
        }
    } else {
        Image<RFLOAT> Istk;
        Istk.read(fn_ref);

        // Check pixel size in the header is consistent with angpix_ref. Otherwise, raise a warning
        RFLOAT angpix_header = Istk.samplingRateX();
        if (verb > 0) {
            if (angpix_ref < 0) {
                if (fabs(angpix_header - angpix) > 1e-3) {
                    std::cerr << " WARNING!!! Pixel size in reference image header= " << angpix_header << " but you have not provided --angpix_ref." << std::endl;
                    std::cerr << " The pixel size of the reference is assumed to be the same as that of the input micrographs (= " << angpix << ")" << std::endl;
                }
            } else {
                if (fabs(angpix_header - angpix_ref) > 1e-3) {
                    std::cerr << " WARNING!!! Pixel size in reference image header= " << angpix_header << " but you have provided --angpix_ref " << angpix_ref << std::endl;
                }
            }
        }

        if (Zsize(Istk()) > 1) {
            if (autopick_helical_segments) {
                REPORT_ERROR("Filament picker (--helix) does not support 3D references. Please use 2D class averages instead.");
            }

            // Re-scale references if necessary
            if (angpix_ref < 0)
                angpix_ref = angpix;

            HealpixSampling sampling;
            sampling.healpix_order = healpix_order;
            sampling.fn_sym = symmetry;
            sampling.perturbation_factor = 0.0;
            sampling.offset_step = 1;
            sampling.limit_tilt = -91.0;
            sampling.is_3D = true;
            sampling.initialise();

            if (verb > 0) {
                std::cout << " Projecting a 3D reference with " << symmetry << " symmetry, using angular sampling rate of "
                          << sampling.getAngularSampling() << " degrees, i.e. in " << sampling.NrDirections() << " directions ... "
                          << std::endl;
            }

            int my_ori_size = Xsize(Istk());
            Projector projector(my_ori_size, TRILINEAR, padding);
            MultidimArray<RFLOAT> dummy;
            int lowpass_size = 2 * ceil(my_ori_size * angpix_ref / lowpass);
            projector.computeFourierTransformMap(Istk(), dummy, lowpass_size);
            MultidimArray<RFLOAT> Mref(my_ori_size, my_ori_size);
            FourierTransformer transformer;
            transformer.setReal(Mref);
            MultidimArray<Complex> &Fref = transformer.getFourier();

            FileName fn_proj = fn_odir + "reference_projections.mrcs";
            for (long int idir = 0; idir < sampling.NrDirections(); idir++) {
                RFLOAT rot  = sampling.rot_angles [idir];
                RFLOAT tilt = sampling.tilt_angles[idir];
                Matrix<RFLOAT> A = Euler::angles2matrix(rot, tilt, 0.0);
                Fref = projector.get2DFourierTransform(Mref.xdim / 2, Mref.ydim, Mref.zdim, A);
                // Shift the image back to the center...
                CenterFFTbySign(Fref);
                transformer.inverseFourierTransform();
                Mrefs.push_back(Mref.setXmippOrigin());

                if (verb > 0) {
                    // Also write out a stack with the 2D reference projections
                    const auto fn_img = FileName::compose(idir + 1, fn_proj);
                    Image<RFLOAT>(Mref).write(fn_img, -1, false, idir == 0 ? WRITE_OVERWRITE : WRITE_APPEND);
                }
            }
        } else {
            // Stack of 2D references
            for (int n = 0; n < Nsize(Istk()); n++) {
                Image<RFLOAT> Iref;
                Istk().getImage(n, Iref());
                Mrefs.push_back(Iref().setXmippOrigin());
            }
        }
    }
    }

    {
    ifdefTIMING(TicToc tt (timer, TIMING_A3);)

    if (!do_LoG) {
        // Re-scale references if necessary
        if (angpix_ref < 0)
            angpix_ref = angpix;

        // Automated determination of bg_radius (same code as in particle_sorter.cpp!)
        if (particle_diameter < 0.0) {
            RFLOAT sumr = 0.0;
            for (const auto &Mref : Mrefs) {
                RFLOAT cornerval = Mref[0];
                // Look on the central X-axis, which first and last values are NOT equal to the corner value
                bool has_set_first = false;
                bool has_set_last = false;
                int first_corner = Xinit(Mref), last_corner = Xlast(Mref);
                for (long int i = Xinit(Mref); i <= Xlast(Mref); i++) {
                    if (!has_set_first) {
                        if (has_set_first = (fabs(Mref.elem(i) - cornerval) > 1e-6)) {
                            first_corner = i;
                        }
                    } else if (!has_set_last) {
                        if (has_set_last = (fabs(Mref.elem(i) - cornerval) < 1e-6)) {
                            last_corner = i - 1;
                        }
                    }
                }
                sumr += last_corner - first_corner;
            }
            particle_diameter = sumr / Mrefs.size();
            // diameter is in Angstroms
            particle_diameter *= angpix_ref;
            if (verb > 0) {
                std::cout << " + Automatically set the background diameter to " << particle_diameter << " Angstrom" << std::endl;
                std::cout << " + You can override this by providing --particle_diameter (in Angstroms)" << std::endl;
            }
        }

        // Now bring Mrefs from angpix_ref to angpix!
        if (fabs(angpix_ref - angpix) > 1e-3) {
            int halfoldsize = Xsize(Mrefs[0]) / 2;
            int newsize = 2 * round(halfoldsize * (angpix_ref / angpix));
            // If the references came from downscaled particles, then those were normalised differently
            // (the stddev is N times smaller after downscaling N times)
            // This needs to be corrected again
            RFLOAT rescale_factor = 1.0;
            if (newsize > Xsize(Mrefs[0]))
                rescale_factor *= (RFLOAT) Xsize(Mrefs[0]) / (RFLOAT) newsize;
            for (auto &ref : Mrefs) {
                resizeMap(ref, newsize);
                ref *= rescale_factor;
                ref.setXmippOrigin();
            }
        }

        // Get particle boxsize from the input reference images
        particle_size = Xsize(Mrefs[0]);

        if (particle_diameter > particle_size * angpix) {
            std::cerr << " mask_diameter (A): " << particle_diameter << " box_size (pix): " << particle_size << " pixel size (A): " << angpix << std::endl;
            REPORT_ERROR("ERROR: the particle mask diameter is larger than the size of the box.");
        }


        if (verb > 0 && autopick_helical_segments) {
            std::cout << " + Helical tube diameter = " << helical_tube_diameter << " Angstroms " << std::endl;
        }
        if (autopick_helical_segments && helical_tube_diameter > particle_diameter) {
            REPORT_ERROR("Error: Helical tube diameter should be smaller than the particle mask diameter!");
        }


        if (autopick_helical_segments && do_amyloid) {

            amyloid_max_psidiff = degrees(helical_tube_curvature_factor_max * 2.0);
            if (verb > 0)
                std::cout << " + Setting amyloid max_psidiff to: " << amyloid_max_psidiff << std::endl;

            if (max_local_avg_diameter < 0.0) {
                max_local_avg_diameter = 3.0 * helical_tube_diameter;
                if (verb > 0)
                    std::cout << " + Setting amyloid max_local_avg_diameter to: " << max_local_avg_diameter << std::endl;
            }
        }

        // Get the squared particle radius (in integer pixels)
        particle_radius2 = round(particle_diameter / (2.0 * angpix));
        particle_radius2 -= decrease_radius;
        particle_radius2 *= particle_radius2;
        #ifdef DEBUG
        std::cerr << " particle_size= " << particle_size << " sqrt(particle_radius2)= " << sqrt(particle_radius2) << std::endl;
        #endif
        // Invert references if necessary (do this AFTER automasking them!)
        if (do_invert) {
            for (auto &ref : Mrefs) { ref *= -1.0; }
        }
    }

    // Get micrograph_size
    Image<RFLOAT> Imic;
    Imic.read(fn_micrographs[0], false);
    micrograph_xsize = Xsize(Imic());
    micrograph_ysize = Ysize(Imic());
    micrograph_size = std::max(micrograph_xsize, micrograph_ysize);
    if (extra_padding > 0) { micrograph_size += 2 * extra_padding; }

    downsize_mic = lowpass < 0.0 ? micrograph_size :
        2 * round(micrograph_size * angpix / lowpass);

    /*
     * Here we set the size of the micrographs during cross-correlation calculation. The final size is still the same size as
     * the input micrographs, we simply adjust the frequencies used in fourier space by cropping the frequency-space images in
     * intermediate calculations.
     */

    if (workFrac > 1) {
        // set size directly
        int tempFrac = round(workFrac);
        tempFrac -= tempFrac % 2;
        if (tempFrac < micrograph_size) {
            workSize = getGoodFourierDims(tempFrac,micrograph_size);
        } else {
            REPORT_ERROR("workFrac larger than micrograph_size (--shrink) cannot be used. Choose a fraction 0<frac<1  OR  size<micrograph_size");
        }
    } else {
        // set size as fraction of original
        if (workFrac > 0) {
            int tempFrac = round(workFrac * (RFLOAT) micrograph_size);
            tempFrac -= tempFrac % 2;
            workSize = getGoodFourierDims(tempFrac, micrograph_size);
        } else if (workFrac == 0) {
            workSize = getGoodFourierDims((int) downsize_mic, micrograph_size);
        } else {
            REPORT_ERROR("negative workFrac (--shrink) cannot be used. Choose a fraction 0<frac<1  OR size<micrograph_size");
        }
    }
    workSize -= workSize % 2; //make even in case it is not already

    if (verb > 0 && workSize < downsize_mic) {
        std::cout << " + WARNING: The calculations will be done at a lower resolution than requested." << std::endl;
    }

    if (
        verb > 0 && autopick_helical_segments && !do_amyloid &&
        float(workSize) / float(micrograph_size) < 0.4999
    ) {
        std::cerr << " + WARNING: Please consider using a shrink value 0.5~1 for picking helical segments. Smaller values may lead to poor results." << std::endl;
    }

    //printf("workSize = %d, corresponding to a resolution of %g for these settings. \n", workSize, 2*(((RFLOAT)micrograph_size*angpix)/(RFLOAT)workSize));

    if (min_particle_distance < 0) {
        min_particle_distance = particle_size * angpix / 2.0;
    }
    }

    {
    ifdefTIMING(TicToc tt (timer, TIMING_A4);)

    // Pre-calculate and store Projectors for all references at the right size
    if (!do_read_fom_maps && !do_LoG) {
        if (verb > 0) {
            std::cout << " Initialising FFTs for the references and masks ... " << std::endl;
        }

        // Calculate a circular mask based on the particle_diameter and then store its FT
        FourierTransformer transformer;
        MultidimArray<RFLOAT> Mcirc_mask(particle_size, particle_size);
        MultidimArray<RFLOAT> Maux(micrograph_size, micrograph_size);
        Mcirc_mask.setXmippOrigin();
        Maux.setXmippOrigin();

        // Sjors 17 Jan 2018
        // Also make a specific circular mask to calculate local average value,
        // for removal of carbon areas with helices.
        if (autopick_helical_segments) {

            Mcirc_mask = 1.0;
            nr_pixels_avg_mask = Mcirc_mask.size();

            long int inner_radius = round(helical_tube_diameter / (2.0 * angpix));
            FOR_ALL_ELEMENTS_IN_ARRAY2D(Mcirc_mask, i, j) {
                if (hypot2(i, j) < inner_radius * inner_radius) {
                    Mcirc_mask.elem(i, j) = 0.0;
                    nr_pixels_avg_mask--;
                }
            }

            if (max_local_avg_diameter > 0) {
                long int outer_radius = round(max_local_avg_diameter / (2.0 * angpix));
                FOR_ALL_ELEMENTS_IN_ARRAY2D(Mcirc_mask, i, j) {
                    if (hypot2(i, j) > outer_radius * outer_radius) {
                        Mcirc_mask.elem(i, j) = 0.0;
                        nr_pixels_avg_mask--;
                    }
                }
            }

            // Now set the mask in the large square and store its FFT
            Maux.initZeros();
            FOR_ALL_ELEMENTS_IN_ARRAY2D(Mcirc_mask, i, j) {
                Maux.elem(i, j) = Mcirc_mask.elem(i, j);
            }
            Favgmsk = transformer.FourierTransform(Maux);
            CenterFFTbySign(Favgmsk);

        }


        // For squared difference, need the mask of the background to locally normalise the micrograph
        nr_pixels_circular_invmask = 0;
        Mcirc_mask.initZeros();
        FOR_ALL_ELEMENTS_IN_ARRAY2D(Mcirc_mask, i, j) {
            if (hypot2(i, j) >= particle_radius2) {
                Mcirc_mask.elem(i, j) = 1.0;
                nr_pixels_circular_invmask++;
            }
        }

        // Now set the mask in the large square and store its FFT
        Maux.initZeros();
        FOR_ALL_ELEMENTS_IN_ARRAY2D(Mcirc_mask, i, j) {
            Maux.elem(i, j) = Mcirc_mask.elem(i, j);
        }
        Finvmsk = transformer.FourierTransform(Maux);
        CenterFFTbySign(Finvmsk);

        // Also get the particle-area mask
        nr_pixels_circular_mask = 0;
        Mcirc_mask.initZeros();
        FOR_ALL_ELEMENTS_IN_ARRAY2D(Mcirc_mask, i, j) {
            if (hypot2(i, j) < particle_radius2) {
                Mcirc_mask.elem(i, j) = 1.0;
                nr_pixels_circular_mask++;
            }
        }

        #ifdef DEBUG
        std::cerr << " min_particle_distance= " << min_particle_distance << " micrograph_size= " << micrograph_size << " downsize_mic= " << downsize_mic << std::endl;
        std::cerr << " nr_pixels_circular_mask= " << nr_pixels_circular_mask << " nr_pixels_circular_invmask= " << nr_pixels_circular_invmask << std::endl;
        #endif

        PPref.clear();
        if (verb > 0)
            init_progress_bar(Mrefs.size());

        Projector PP(micrograph_size, TRILINEAR, padding);
        MultidimArray<RFLOAT> dummy;

        for (int iref = 0; iref < Mrefs.size(); iref++) {

            // (Re-)apply the mask to the references
            Mrefs[iref] *= Mcirc_mask;

            // Set reference in the large box of the micrograph
            Maux.initZeros();
            Maux.setXmippOrigin();
            FOR_ALL_ELEMENTS_IN_ARRAY2D(Mrefs[iref], i, j) {
                Maux.elem(i, j) = Mrefs[iref].elem(i, j);
            }

            // And compute its Fourier Transform inside the Projector
            PP.computeFourierTransformMap(Maux, dummy, downsize_mic, 1, false);
            PPref.push_back(PP);

            if (verb > 0)
                progress_bar(iref + 1);

        }

        if (verb > 0)
            progress_bar(Mrefs.size());

    }
    }

    }
    #ifdef DEBUG
    std::cerr << "Finishing initialise" << std::endl;
    #endif
}

#ifdef CUDA
int AutoPicker::deviceInitialise() {
    int devCount;
    cudaGetDeviceCount(&devCount);

    const auto allThreadIDs = untangleDeviceIDs(gpu_ids);

    // Sequential initialisation of GPUs on all ranks
    const int dev_id = std::isdigit(*gpu_ids.begin()) ?
        textToInteger(allThreadIDs[0][0].c_str()) :
        0;

    if (verb > 0) {
        std::cout << " + Using GPU device " << dev_id << std::endl;
    }

    return dev_id;
}
#endif

void AutoPicker::run() {
    int barstep;
    if (verb > 0) {
        std::cout << " Autopicking ..." << std::endl;
        init_progress_bar(fn_micrographs.size());
        barstep = std::max(1, (int) fn_micrographs.size() / 60);
    }

    FileName fn_olddir = "";
    for (long int imic = 0; imic < fn_micrographs.size(); imic++) {

        // Abort through the pipeline_control system
        if (pipeline_control_check_abort_job())
            exit(RELION_EXIT_ABORTED);

        if (verb > 0 && imic % barstep == 0)
            progress_bar(imic);

        // Check new-style outputdirectory exists and make it if not!
        FileName fn_dir = getOutputRootName(fn_micrographs[imic]);
        fn_dir = fn_dir.beforeLastOf("/");
        if (fn_dir != fn_olddir) {
            // Make a Particles directory
            int res = system(("mkdir -p " + fn_dir).c_str());
            fn_olddir = fn_dir;
        }

        {
        ifdefTIMING(TicToc tt (timer, TIMING_A5);)
        if (do_LoG) {
            autoPickLoGOneMicrograph(fn_micrographs[imic], imic);
        } else {
            autoPickOneMicrograph   (fn_micrographs[imic], imic);
        }
        }
    }

    if (verb > 0)
        progress_bar(fn_micrographs.size());
}

static RFLOAT mean(MetaDataTable &mdt, EMDL::EMDLabel label, long int n) {
    RFLOAT mu = 0.0;
    for (long int i : mdt) {
        mu += mdt.getValue<RFLOAT>(label, i);
    }
    return mu / (RFLOAT) n;
}

void AutoPicker::generatePDFLogfile() {

    long int barstep = std::max(1l, (long int) fn_ori_micrographs.size() / 60);
    if (verb > 0) {
        std::cout << " Generating logfile.pdf ... " << std::endl;
        init_progress_bar(fn_ori_micrographs.size());
    }

    MetaDataTable MDresult;
    long total_nr_picked = 0;
    for (long int imic = 0; imic < fn_ori_micrographs.size(); imic++) {
        MetaDataTable MD;
        FileName fn_pick = getOutputRootName(fn_ori_micrographs[imic]) + "_" + fn_out + ".star";
        if (exists(fn_pick)) {
            MD.read(fn_pick);
            long nr_pick = MD.size();
            total_nr_picked += nr_pick;
            if (MD.containsLabel(EMDL::PARTICLE_AUTOPICK_FOM)) {

                RFLOAT avg_fom = mean(MD, EMDL::PARTICLE_AUTOPICK_FOM, nr_pick);

                // Abuse MetadataTable to conveniently make histograms and value-plots
                const long int i = MDresult.addObject();
                MDresult.setValue(EMDL::MICROGRAPH_NAME, fn_ori_micrographs[imic], i);
                MDresult.setValue(EMDL::PARTICLE_AUTOPICK_FOM, avg_fom, i);
                MDresult.setValue(EMDL::MLMODEL_GROUP_NR_PARTICLES, nr_pick, i);
            }
        }

        if (verb > 0 && imic % 60 == 0) progress_bar(imic);

    }

    if (verb > 0) {
        progress_bar(fn_ori_micrographs.size());
        std::cout << " Total number of particles from " << fn_ori_micrographs.size() << " micrographs is " << total_nr_picked << std::endl;
        if (!fn_ori_micrographs.empty()) {
            long avg = round((RFLOAT) total_nr_picked / fn_ori_micrographs.size());
            std::cout << " i.e. on average there were " << avg << " particles per micrograph" << std::endl;
        }
    }

    // Values for all micrographs
    std::vector<FileName> all_fn_eps;
    std::vector<RFLOAT> histX, histY;

    MDresult.write(fn_odir + "summary.star");
    {
        std::unique_ptr<CPlot2D> plot (new CPlot2D("Nr of picked particles for all micrographs"));
        PlotMetaData::addToCPlot2D(MDresult, plot.get(), EMDL::UNDEFINED, EMDL::MLMODEL_GROUP_NR_PARTICLES, 1.0);
        plot->SetDrawLegend(false);
        FileName fn_eps = fn_odir + "all_nr_parts.eps";
        plot->OutputPostScriptPlot(fn_eps);
        all_fn_eps.push_back(fn_eps);
    }
    if (MDresult.size() > 3) {
        std::unique_ptr<CPlot2D> plot (new CPlot2D(""));
        PlotMetaData::columnHistogram(MDresult, EMDL::MLMODEL_GROUP_NR_PARTICLES, histX, histY, 0, plot.get());
        FileName fn_eps = fn_odir + "histogram_nrparts.eps";
        plot->SetTitle("Histogram of nr of picked particles per micrograph");
        plot->OutputPostScriptPlot(fn_eps);
        all_fn_eps.push_back(fn_eps);
    }

    {
        std::unique_ptr<CPlot2D> plot (new CPlot2D("Average autopick FOM for all micrographs"));
        PlotMetaData::addToCPlot2D(MDresult, plot.get(), EMDL::UNDEFINED, EMDL::PARTICLE_AUTOPICK_FOM, 1.0);
        plot->SetDrawLegend(false);
        FileName fn_eps = fn_odir + "all_FOMs.eps";
        plot->OutputPostScriptPlot(fn_eps);
        all_fn_eps.push_back(fn_eps);
    }
    if (MDresult.size() > 3) {
        std::unique_ptr<CPlot2D> plot (new CPlot2D(""));
        PlotMetaData::columnHistogram(MDresult, EMDL::PARTICLE_AUTOPICK_FOM, histX, histY, 0, plot.get());
        FileName fn_eps = fn_odir + "histogram_FOMs.eps";
        plot->SetTitle("Histogram of average autopick FOM per micrograph");
        plot->OutputPostScriptPlot(fn_eps);
        all_fn_eps.push_back(fn_eps);
    }

    joinMultipleEPSIntoSinglePDF(fn_odir + "logfile.pdf", all_fn_eps);

    if (verb > 0) {
        std::cout << " Done! Written: " << fn_odir << "logfile.pdf " << std::endl;
    }

}

std::vector<AmyloidCoord> AutoPicker::findNextCandidateCoordinates(
    AmyloidCoord &mycoord, std::vector<AmyloidCoord> &circle,
    RFLOAT threshold_value, RFLOAT max_psidiff, int skip_side, float scale,
    MultidimArray<RFLOAT> &Mccf, MultidimArray<RFLOAT> &Mpsi
) {

    std::vector<AmyloidCoord> result;

    int new_micrograph_xsize = (float) micrograph_xsize * scale;
    int new_micrograph_ysize = (float) micrograph_ysize * scale;
    int skip_side_pix = round(skip_side * scale);
    Vector<RFLOAT> vec_c (2), vec_p (2);
    Matrix<RFLOAT> A2D = rotation2DMatrix(-mycoord.psi, false);

    for (const auto& coord: circle) {
        // Rotate the circle-vector coordinates along the mycoord.psi
        vec_c[0] = coord.x;
        vec_c[1] = coord.y;
        vec_p = matmul(A2D, vec_c);

        long int jj = round(mycoord.x + vec_p[0]);
        long int ii = round(mycoord.y + vec_p[1]);

        if (
            jj >= Xmipp::init(new_micrograph_xsize) + skip_side_pix + 1 &&
            jj <  Xmipp::last(new_micrograph_xsize) - skip_side_pix - 1 &&
            ii >= Xmipp::init(new_micrograph_ysize) + skip_side_pix + 1 &&
            ii <  Xmipp::last(new_micrograph_ysize) - skip_side_pix - 1
        ) {
            RFLOAT myccf = Mccf.elem(ii, jj);
            RFLOAT mypsi = Mpsi.elem(ii, jj);

            // Small difference in psi-angle with mycoord
            RFLOAT psidiff = wrap(fabs(mycoord.psi - mypsi), 0.0, 360.0);
            if (psidiff > 180.0) { psidiff -= 180.0; }
            if (psidiff >  90.0) { psidiff -= 180.0; }

            if (fabs(psidiff) < max_psidiff && myccf > threshold_value) {
                result.push_back({
                    mycoord.x + XX(vec_p), mycoord.y + YY(vec_p),
                    Mpsi.elem(ii, jj), myccf
                });
            }
        }
    }
    return result;
}



AmyloidCoord AutoPicker::findNextAmyloidCoordinate(
    AmyloidCoord &mycoord, std::vector<AmyloidCoord> &circle, RFLOAT threshold_value,
    RFLOAT max_psidiff, RFLOAT amyloid_diameter_pix, int skip_side, float scale,
    MultidimArray<RFLOAT> &Mccf, MultidimArray<RFLOAT> &Mpsi
) {

    int new_micrograph_xsize = (float) micrograph_xsize * scale;
    int new_micrograph_ysize = (float) micrograph_ysize * scale;
    int skip_side_pix = round(skip_side * scale);

    // Return if this one has been done already..
    AmyloidCoord result;
    result.x = result.y = result.psi = 0.0;
    result.fom = -999.0;
    // Why yx and not xy?
    if (Mccf.elem((int) round(mycoord.x), (int) round(mycoord.y)) < threshold_value)
        return result;

    // Set FOM to small value in circle around mycoord
    int myrad = round(0.5 * helical_tube_diameter / angpix * scale);
    float myrad2 = (float) myrad * (float) myrad;
    for (int ii = -myrad; ii <= myrad; ii++)
    for (int jj = -myrad; jj <= myrad; jj++) {
        float r2 = (float) (ii * ii) + (float) (jj * jj);
        if (r2 < myrad2) {

            long int jp = round(mycoord.x + jj);
            long int ip = round(mycoord.y + ii);
            // std::cerr << " jp= " << jp << " ip= " << ip << " jj= " << jj  << " ii= " << ii<< std::endl;
            // std::cerr << " Xmipp::init(new_micrograph_xsize)= " << Xmipp::init(new_micrograph_xsize) + skip_side_pix + 1<< " Xmipp::last(new_micrograph_xsize)= " << Xmipp::last(new_micrograph_xsize)- skip_side_pix - 1 << std::endl;
            // std::cerr << " Xmipp::init(new_micrograph_ysize)= " << Xmipp::init(new_micrograph_ysize) + skip_side_pix + 1<< " Xmipp::last(new_micrograph_ysize)= " << Xmipp::last(new_micrograph_ysize)- skip_side_pix - 1 << std::endl;
            if (
                ip >= Xmipp::init(Xsize(Mccf)) &&
                ip <= Xmipp::last(Xsize(Mccf)) &&
                jp >= Xmipp::init(Ysize(Mccf)) &&
                jp <= Xmipp::last(Ysize(Mccf))
            ) {
                Mccf.elem(ip, jp) = -999.0;
            }
        }
    }

    // See how far we can grow in any of the circle directions...
    // Recursive call to findNextCandidateCoordinates...
    // Let's search 3 layers deep...
    std::vector<AmyloidCoord> new1coords = findNextCandidateCoordinates(mycoord, circle, threshold_value, max_psidiff, skip_side, scale, Mccf, Mpsi);

    long int N = new1coords.size();
    std::vector<int> max_depths(N, 0);
    std::vector<RFLOAT> max_sumfoms(N, -9999.0);

    RFLOAT sumfom = 0.0;
    RFLOAT max_sumfom = -9999.0;
    int best_inew1 = -1;
    for (int inew1 = 0; inew1 < new1coords.size(); inew1++) {
        auto &coord1 = new1coords[inew1];
        sumfom = coord1.fom;
        if (sumfom > max_sumfom) {
            max_sumfom = sumfom;
            best_inew1 = inew1;
        }

        std::vector<AmyloidCoord> new2coords = findNextCandidateCoordinates(
            coord1, circle, threshold_value, max_psidiff,
            skip_side, scale, Mccf, Mpsi
        );
        for (auto &coord2 : new2coords) {
            sumfom = coord1.fom + coord2.fom;
            if (sumfom > max_sumfom) {
                max_sumfom = sumfom;
                best_inew1 = inew1;
            }

            std::vector<AmyloidCoord> new3coords = findNextCandidateCoordinates(
                coord2, circle, threshold_value, max_psidiff,
                skip_side, scale, Mccf, Mpsi
            );
            for (auto &coord3 : new3coords) {
                sumfom = coord1.fom + coord2.fom + coord3.fom;
                if (sumfom > max_sumfom) {
                    max_sumfom = sumfom;
                    best_inew1 = inew1;
                }

                std::vector<AmyloidCoord> new4coords = findNextCandidateCoordinates(
                    coord3, circle, threshold_value, max_psidiff,
                    skip_side, scale, Mccf, Mpsi
                );
                for (auto &coord4 : new4coords) {
                    sumfom = coord1.fom + coord2.fom + coord3.fom + coord4.fom;
                    if (sumfom > max_sumfom) {
                        max_sumfom = sumfom;
                        best_inew1 = inew1;
                    }
                }
            }
        }
    }

    if (best_inew1 < 0) {
        return result;
    } else {
        /*
        RFLOAT prevpsi = (best_inew1 > 0) ? new1coords[best_inew1-1].psi : -99999.0;
        RFLOAT nextpsi = (new1coords.size() - best_inew1 > 1) ? new1coords[best_inew1+1].psi : -99999.0;

        RFLOAT nextpsidiff = -9999., prevpsidiff=-9999.0;
        if (prevpsi > -999.)
        {
            RFLOAT psidiff = fabs(mycoord.psi - prevpsi);
            psidiff = wrap(psidiff, 0., 360.);
            if (psidiff > 180.)
                    psidiff -= 180.0;
            if (psidiff > 90.)
                    psidiff -= 180.0;
            prevpsidiff = psidiff;
        }
        if (nextpsi > -999.)
        {
            RFLOAT psidiff = fabs(mycoord.psi - nextpsi);
            psidiff = wrap(psidiff, 0., 360.);
            if (psidiff > 180.)
                    psidiff -= 180.0;
            if (psidiff > 90.)
                    psidiff -= 180.0;
            nextpsidiff = psidiff;
        }



        std::cerr << " new1coords[best_inew1].fom= " << new1coords[best_inew1].fom
                << " x= " << new1coords[best_inew1].x
                << " y= " << new1coords[best_inew1].y
                << " myx= " << mycoord.x
                << " myy= " << mycoord.y
                << " mypsi= " << mycoord.psi
                << " new1coords[best_inew1].psi= " << new1coords[best_inew1].psi
                << " prevpsi= " << prevpsi << " prevpsidiff= " << prevpsidiff
                << " nextpsi= " << nextpsi << " nextpsidiff= " << nextpsidiff
                << std::endl;
        */
        return new1coords[best_inew1];
    }
}



void AutoPicker::pickAmyloids(
    MultidimArray<RFLOAT> &Mccf, MultidimArray<RFLOAT> &Mpsi,
    MultidimArray<RFLOAT> &Mstddev, MultidimArray<RFLOAT> &Mavg,
    RFLOAT threshold_value, RFLOAT max_psidiff,
    FileName &fn_mic_in, FileName &fn_star_out,
    RFLOAT amyloid_width, int skip_side, float scale
) {

    // Set up a vector with coordinates of feasible next coordinates regarding distance and psi-angle
    std::vector<AmyloidCoord> circle;
    int myrad = round(0.5 * helical_tube_diameter / angpix * scale);
    int myradb = myrad + 1;
    float myrad2  = (float) myrad  * (float) myrad;
    float myradb2 = (float) myradb * (float) myradb;
    for (int ii = -myradb; ii <= myradb; ii++) {
    for (int jj = -myradb; jj <= myradb; jj++) {
        float r2 = ii * ii + jj * jj;
        if (r2 > myrad2 && r2 <= myradb2) {
            float psi_deg = degrees(atan2((float) ii, (float) jj));
            if (psi_deg > +90.0) { psi_deg -= 180.0; }
            if (psi_deg < -90.0) { psi_deg += 180.0; }
            if (fabs(psi_deg) < max_psidiff) {
                circle.push_back({(RFLOAT) jj, (RFLOAT) ii, psi_deg, 0.0});
            }
        }
    }
    }

    using Helix = std::vector<AmyloidCoord>;

    std::vector<Helix> helices;
    bool ccf_peaks_remaining = true;
    while (ccf_peaks_remaining) {
        const auto it = std::max_element(Mccf.begin(), Mccf.end());
        const float ccf = *it;
        const auto direct_indices = offset_to_direct_indices(Mccf, it - Mccf.begin());
        const auto Xmipp_indices = direct_indices_to_Xmipp_indices(Mccf, direct_indices[0], direct_indices[1], direct_indices[2], direct_indices[3]);
        const long int imax = Xmipp_indices[0], jmax = Xmipp_indices[1];
        const float psi = Mpsi.elem(imax, jmax);

        // Stop searching if all pixels are below min_ccf!
        // std::cerr << " ccf= " << ccf << " imax= " << imax << " jmax= " << jmax << std::endl;
        // std::cerr << " helices.size()= " << helices.size() << " threshold_value= " << threshold_value << " psi= " << psi << std::endl;
        if (ccf < threshold_value) { ccf_peaks_remaining = false; }

        AmyloidCoord coord {(RFLOAT) imax, (RFLOAT) jmax, psi, ccf};

        Helix helix {coord};

        bool is_done_start = false;
        bool is_done_end = false;
        while (!is_done_start || !is_done_end) {
            if (!is_done_start) {
                AmyloidCoord newcoord = findNextAmyloidCoordinate(
                    helix[0], circle, threshold_value, max_psidiff,
                    helical_tube_diameter / angpix, round(skip_side), scale, Mccf, Mpsi
                );
                // std::cerr << " START newcoord.x= " << newcoord.x << " newcoord.y= " << newcoord.y << " newcoord.fom= " << newcoord.fom
                //     << " stddev = " << Mstddev.elem(round(newcoord.x), round(newcoord.y))
                //     << " avg= " <<	Mavg.elem(round(newcoord.x), round(newcoord.y))	<< std::endl;
                // Also check for Mstddev value
                if (
                    newcoord.fom > threshold_value &&
                    (max_stddev_noise <=    0.0 || Mstddev.elem((int) round(newcoord.x), (int) round(newcoord.y)) <= max_stddev_noise) &&
                    (min_avg_noise    <= -900.0 || Mavg   .elem((int) round(newcoord.x), (int) round(newcoord.y)) >= min_avg_noise)
                ) {
                    helix.insert(helix.begin(), newcoord);
                } else {
                    is_done_start = true;
                }
            }
            if (!is_done_end) {
                AmyloidCoord newcoord = findNextAmyloidCoordinate(
                    helix[helix.size() - 1], circle, threshold_value, max_psidiff,
                    helical_tube_diameter / angpix, round(skip_side), scale, Mccf, Mpsi
                );
                // std::cerr << " END newcoord.x= " << newcoord.x << " newcoord.y= " << newcoord.y << " newcoord.fom= " << newcoord.fom << std::endl;
                if (
                    newcoord.fom > threshold_value &&
                    (max_stddev_noise <=    0.0 || Mstddev.elem((int) round(newcoord.x), (int) round(newcoord.y)) <= max_stddev_noise) &&
                    (min_avg_noise    <= -900.0 || Mavg   .elem((int) round(newcoord.x), (int) round(newcoord.y)) >= min_avg_noise)
                ) {
                    helix.push_back(newcoord);
                } else {
                    is_done_end = true;
                }
            }
            // std::cerr << " is_done_start= " << is_done_start << " is_done_end= " << is_done_end << std::endl;
        }

        // std::cerr << " helix.size()= " << helix.size() << std::endl;
        if (helical_tube_diameter * 0.5 * helix.size() > helical_tube_length_min) {
            helices.push_back(helix);
            /*
            std::cerr << "PUSHING BACK HELIX " << helices.size() << " << WITH SIZE= " << helix.size() << std::endl;
            char c;
            std::cerr << " helices.size()= " << helices.size() << std::endl;
            std::cerr << "press any key" << std::endl;
            //std::cin >> c;
            Image<RFLOAT> It (Mccf);
            It.write("Mccf.spi");

            // TMP
            // ccf_peaks_remaining=false;
             */
        }

    }

    // Now write out in a STAR file
    // Write out a STAR file with the coordinates
    MetaDataTable MDout;

    // Only output STAR header if there are no tubes...
    MDout.clear();
    MDout.addLabel(EMDL::IMAGE_COORD_X);
    MDout.addLabel(EMDL::IMAGE_COORD_Y);
    MDout.addLabel(EMDL::PARTICLE_AUTOPICK_FOM);
    MDout.addLabel(EMDL::PARTICLE_HELICAL_TUBE_ID);
    MDout.addLabel(EMDL::ORIENT_TILT_PRIOR);
    MDout.addLabel(EMDL::ORIENT_PSI_PRIOR);
    MDout.addLabel(EMDL::PARTICLE_HELICAL_TRACK_LENGTH_ANGSTROM);
    MDout.addLabel(EMDL::ORIENT_PSI_PRIOR_FLIP_RATIO);
    MDout.addLabel(EMDL::ORIENT_ROT_PRIOR_FLIP_RATIO);	// KThurber

    float interbox_dist = min_particle_distance / angpix;
    // Write out segments all all helices
    int helixid = 0;
    for (int ihelix = 0; ihelix < helices.size(); ihelix++) {
        const Helix &helix = helices[ihelix];
        RFLOAT leftover_dist = 0.0;
        RFLOAT tube_length = 0.0;
        for (long int iseg = 0; iseg < helix.size() - 1; iseg++) {
        // for (long int iseg = 0; iseg < helix.size(); iseg++) {

            /*

                RFLOAT xval = (helix[iseg].x / scale) - (RFLOAT)(Xmipp::init(micrograph_xsize));
                RFLOAT yval = (helix[iseg].y / scale) - (RFLOAT)(Xmipp::init(micrograph_ysize));
                const long int i = MDout.addObject();
                MDout.setValue(EMDL::IMAGE_COORD_X, xval, i);
                MDout.setValue(EMDL::IMAGE_COORD_Y, yval, i);
                MDout.setValue(EMDL::PARTICLE_HELICAL_TUBE_ID, ihelix+1, i); // start counting at 1
                MDout.setValue(EMDL::ORIENT_PSI_PRIOR, helix[iseg].psi, i);
                */

            // Distance to next segment
            float dx = (float) (helix[iseg + 1].x - helix[iseg].x) / scale;
            float dy = (float) (helix[iseg + 1].y - helix[iseg].y) / scale;
            float distnex = hypot(dx, dy);
            float psi = -atan2(dy, dx);
            for (float position = leftover_dist; position < distnex; position += interbox_dist) {
                RFLOAT frac = position / distnex;
                RFLOAT xval = helix[iseg].x / scale - (RFLOAT) Xmipp::init(micrograph_xsize) + frac * dx;
                RFLOAT yval = helix[iseg].y / scale - (RFLOAT) Xmipp::init(micrograph_ysize) + frac * dy;

                const long int i = MDout.addObject();
                MDout.setValue(EMDL::IMAGE_COORD_X, xval, i);
                MDout.setValue(EMDL::IMAGE_COORD_Y, yval, i);
                MDout.setValue(EMDL::PARTICLE_AUTOPICK_FOM, helix[iseg].fom, i);
                MDout.setValue(EMDL::PARTICLE_HELICAL_TUBE_ID, ihelix + 1, i); // start counting at 1
                MDout.setValue(EMDL::ORIENT_TILT_PRIOR, 90.0, i);
                MDout.setValue(EMDL::ORIENT_PSI_PRIOR, degrees(psi), i);
                MDout.setValue(EMDL::PARTICLE_HELICAL_TRACK_LENGTH_ANGSTROM, angpix * tube_length, i);
                MDout.setValue(EMDL::ORIENT_PSI_PRIOR_FLIP_RATIO, 0.5, i);
                MDout.setValue(EMDL::ORIENT_ROT_PRIOR_FLIP_RATIO, 0.5, i);	// KThurber

                leftover_dist = interbox_dist + (distnex - position);
                tube_length += interbox_dist;
            }
        }
        helixid++;
    }

    MDout.write(getOutputRootName(fn_mic_in) + "_" + fn_star_out + ".star");

}

void AutoPicker::pickCCFPeaks(
    const MultidimArray<RFLOAT> &Mccf,
    const MultidimArray<RFLOAT> &Mstddev, const MultidimArray<RFLOAT> &Mavg,
    const MultidimArray<int> &Mclass,
    RFLOAT threshold_value, int peak_r_min, RFLOAT particle_diameter_pix,
    std::vector<ccfPeak>& ccf_peak_list,
    MultidimArray<RFLOAT> &Mccfplot,
    int skip_side, float scale
) {
    int new_micrograph_xsize = (float) micrograph_xsize * scale;
    int new_micrograph_ysize = (float) micrograph_ysize * scale;
    int nr_pixels;
    RFLOAT ratio;

    // Rescale skip_side and particle_diameter_pix
    skip_side = (float) skip_side * scale;
    particle_diameter_pix *= scale;
    // int micrograph_core_size = std::min(micrograph_xsize, micrograph_ysize) - skip_side * 2 - 2;

    if (Nsize(Mccf) != 1 || Zsize(Mccf) != 1 || Ysize(Mccf) != Xsize(Mccf))
        REPORT_ERROR("autopicker.cpp::pickCCFPeaks: The micrograph should be a 2D square!");
    if (Xsize(Mccf) < new_micrograph_xsize || Ysize(Mccf) < new_micrograph_ysize)
        REPORT_ERROR("autopicker.cpp::pickCCFPeaks: Invalid dimensions for Mccf!");
    // if (micrograph_core_size < 100 * scale)
    // 	REPORT_ERROR("autopicker.cpp::pickCCFPeaks: The micrograph is too small relative to the particle box!");
    if (Yinit(Mccf) != Xmipp::init(Ysize(Mccf)) || Xinit(Mccf) != Xmipp::init(Xsize(Mccf)))
        REPORT_ERROR("autopicker.cpp::pickCCFPeaks: The origin of input 3D MultidimArray is not at the center (use v.setXmippOrigin() before calling this function)!");
    if (!Mccf.sameShape(Mclass))
        REPORT_ERROR("autopicker.cpp::pickCCFPeaks: Mccf and Mclass should have the same shape!");
    if (peak_r_min < 1)
        REPORT_ERROR("autopicker.cpp::pickCCFPeaks: Radii of peak should be positive!");
    if (particle_diameter_pix < 5.0 * scale)
        REPORT_ERROR("autopicker.cpp::pickCCFPeaks: Particle diameter should be larger than 5 pixels!");

    // Init output
    ccf_peak_list.clear();

    const auto stats = computeStats(Mccf);

    // Collect all high ccf pixels
    std::vector<ccfPixel> ccf_pixel_list;
    MultidimArray<int> Mrec = MultidimArray<int>::zeros(Mccf);
    Mrec.setXmippOrigin();
    nr_pixels = 0;
    for (int jj = Xmipp::init(new_micrograph_ysize) + skip_side; jj <= Xmipp::last(new_micrograph_ysize) - skip_side; jj++)
    for (int ii = Xmipp::init(new_micrograph_xsize) + skip_side; ii <= Xmipp::last(new_micrograph_xsize) - skip_side; ii++) {
        // Only check stddev in the noise areas if max_stddev_noise is positive!
        if (max_stddev_noise >    0.0 && Mstddev.elem(ii, jj) > max_stddev_noise) continue;
        if (min_avg_noise    > -900.0 &&    Mavg.elem(ii, jj) < min_avg_noise)    continue;

        RFLOAT fom = Mccf.elem(ii, jj);
        nr_pixels++;
        if (fom > threshold_value) {
            Mrec.elem(ii, jj) = 1;
            ccf_pixel_list.emplace_back(ii, jj, fom);
        }
    }
    std::sort(ccf_pixel_list.begin(), ccf_pixel_list.end());
    #ifdef DEBUG_HELIX
    std::cout << " nr_high_ccf_pixels= " << ccf_pixel_list.size() << std::endl;
    #endif

    // Do not do anything if nr_high_ccf_pixels is too small or too large! Thres is restricted to 0.01%-10% beforehand.
    if (nr_pixels < 100 || ccf_pixel_list.size() < 10) {
        ccf_peak_list.clear();
        return;
    }
    ratio = (RFLOAT) ccf_pixel_list.size() / (RFLOAT) nr_pixels;
    #ifdef DEBUG_HELIX
    std::cout << " ratio= " << ratio << std::endl;
    #endif
    // Sjors changed ratio threshold to 0.5 on 21nov2017 for tau filaments
    //if (ratio > 0.1)
    if (ratio > 0.5) {
        ccf_peak_list.clear();
        return;
    }

    // Find all peaks! (From the highest fom values)
    ccf_peak_list.clear();
    for (int id = ccf_pixel_list.size() - 1; id >= 0; id--) {
        int x_new, y_new, x_old, y_old, rmax, rmax2, iref;
        int rmax_min = peak_r_min;
        int rmax_max;
        int iter_max = 3;
        RFLOAT fom_max;
        RFLOAT area_percentage_min = 0.8;

        // Deal with very small shrink values. But it still not performs well if workFrac < 0.2
        if (scale < 0.5 && scale > 0.2) {
            area_percentage_min = 0.2 + (2.0 * (scale - 0.2));
        } else if (scale < 0.2) {
            area_percentage_min = 0.2;
        }

        // Check if this ccf pixel is covered by another peak
        x_old = x_new = round(ccf_pixel_list[id].x);
        y_old = y_new = round(ccf_pixel_list[id].y);
        if (Mrec.elem(x_new, y_new) == 0)
            continue;

        iref = Mclass.elem(x_new, y_new);
        fom_max = Mccf.elem(x_new, y_new);

        // Pick a peak starting from this ccf pixel
        ccfPeak ccf_peak_small, ccf_peak_big;
        rmax_max = round(particle_diameter_pix / 2.0); // 29 Sep 2015 ????????????
        // Sjors 21 Nov 2017 try to adapt for tau fibrils ...
        //if (rmax_max < 100)
        //	rmax_max = 100;
        for (rmax = rmax_min; rmax < rmax_max; rmax++) {
            // Record the smaller peak
            ccf_peak_small = ccf_peak_big;

            // std::cout << " id= " << id << ", rmax= " << rmax << ", p= " << ccf_peak_small.area_percentage << std::endl;

            // 5 iterations to guarantee convergence??????????????
            // Require 5 iterations for stablising the center of this peak under this rmax
            for (int iter = 0; iter < iter_max; iter++) {
                // Empty this peak
                ccf_peak_big.clear();

                // New rmax
                rmax2 = rmax * rmax;

                // Get all ccf pixels within this rmax
                for (int dx = -rmax; dx <= rmax; dx++)
                for (int dy = -rmax; dy <= rmax; dy++) {
                    // Boundary checks
                    if (dx * dx + dy * dy > rmax2)
                        continue;

                    x_new = x_old + dx;
                    y_new = y_old + dy;

                    if (
                        x_new < Xmipp::init(new_micrograph_xsize) + skip_side + 1 ||
                        x_new > Xmipp::last(new_micrograph_xsize) - skip_side - 1 ||
                        y_new < Xmipp::init(new_micrograph_ysize) + skip_side + 1 ||
                        y_new > Xmipp::last(new_micrograph_ysize) - skip_side - 1
                    ) continue;

                    // Push back all ccf pixels within this rmax
                    RFLOAT ccf = Mccf.elem(x_new, y_new);
                    if (Mrec.elem(x_new, y_new) == 0)
                        ccf = stats.min;
                    ccf_peak_big.ccf_pixel_list.push_back({(RFLOAT) x_new, (RFLOAT) y_new, ccf});
                }
                // Check ccf_peak.ccf_pixel_list.size() below!

                // Refresh
                ccf_peak_big.r = rmax;
                ccf_peak_big.fom_thres = threshold_value;
                if (!ccf_peak_big.refresh()) {
                    // std::cout << " x_old, y_old = " << x_old << ", " << y_old << std::endl;
                    // REPORT_ERROR("autopicker.cpp::CFFPeaks(): BUG No ccf pixels found within the small circle!");
                    break;
                }
                x_new = round(ccf_peak_big.x);
                y_new = round(ccf_peak_big.y);

                // Out of range
                if (
                    x_new < (Xmipp::init(new_micrograph_xsize) + skip_side + 1) ||
                    x_new > (Xmipp::last(new_micrograph_xsize) - skip_side - 1) ||
                    y_new < (Xmipp::init(new_micrograph_ysize) + skip_side + 1) ||
                    y_new > (Xmipp::last(new_micrograph_ysize) - skip_side - 1)
                ) break;

                // Convergence
                if (x_old == x_new && y_old == y_new) break;

                x_old = x_new;
                y_old = y_new;

            }

            // Peak finding is over if peak area does not expand
            if (ccf_peak_big.area_percentage < area_percentage_min) break;

        }

        // A peak is found
        if (ccf_peak_small.isValid()) {
            for (const ccfPixel &pixel : ccf_peak_small.ccf_pixel_list) {
                Mrec.elem(round(pixel.x), round(pixel.y)) = 0;
            }
            // TODO: if r > ...? do not include this peak?
            ccf_peak_small.ref = iref;
            ccf_peak_small.fom_max = fom_max;
            ccf_peak_list.push_back(ccf_peak_small);
            // std::cout << ccf_peak_list.size() << ", "<< std::flush;
        }
    }
    // Sort the peaks from the weakest to the strongest
    std::sort(ccf_peak_list.begin(), ccf_peak_list.end());
    #ifdef DEBUG_HELIX
    std::cout << " nr_peaks= " << ccf_peak_list.size() << std::endl;
    #endif

    // Remove too close peaks (retain the stronger ones while remove the weaker)
    Mrec.clear();
    Mrec.resize(Mccf);
    (Mrec = -1).setXmippOrigin();
    // Sort the peaks from the weakest to the strongest
    for (int new_id = 0; new_id < ccf_peak_list.size(); new_id++) {
        ccfPeak &peak = ccf_peak_list[new_id];
        RFLOAT peak_r2 = peak.r * peak.r;
        int peak_r = peak.r > 0.0 ? ceil(peak.r) : -1;

        // Remove peaks with too small/big radii!
        if (peak_r <= 1 || peak_r > particle_diameter_pix / 2.0) {
            peak.r = -1.0;
            continue;
        }
        for (int dx = -peak_r; dx <= peak_r; dx++)
        for (int dy = -peak_r; dy <= peak_r; dy++) {
            if ((RFLOAT) (dx * dx + dy * dy) > peak_r2)
                continue;

            int x = dx + round(peak.x);
            int y = dy + round(peak.y);

            // Out of range
            if (
                x < (Xmipp::init(new_micrograph_xsize) + skip_side + 1) ||
                x > (Xmipp::last(new_micrograph_xsize) - skip_side - 1) ||
                y < (Xmipp::init(new_micrograph_ysize) + skip_side + 1) ||
                y > (Xmipp::last(new_micrograph_ysize) - skip_side - 1)
            ) continue;

            int old_id = Mrec.elem(x, y);
            if (old_id >= 0) { ccf_peak_list[old_id].r = -1.0; }
            Mrec.elem(x, y) = new_id;
        }
    }

    // Remove invalid peaks
    ccf_peak_list.erase(std::remove_if(
        ccf_peak_list.begin(), ccf_peak_list.end(),
        [] (ccfPeak peak) { return !peak.isValid(); }
    ), ccf_peak_list.end());
    #ifdef DEBUG_HELIX
    std::cout << " Number of peaks after pruning = " << ccf_peak_list.size() << std::endl;
    #endif

    // TODO: Remove all discrete peaks (one peak should have at least two neighbouring peaks within r=particle_radius)

    // Plot
    Mccfplot.clear();
    Mccfplot.resize(Mccf);
    Mccfplot.initZeros();
    Mccfplot.setXmippOrigin();
    for (const ccfPeak &peak : ccf_peak_list)
    for (const ccfPixel &pixel : peak.ccf_pixel_list) {
        if (pixel.fom < peak.fom_thres) continue;
        Mccfplot.elem(round(pixel.x), round(pixel.y)) = 1.0;
    }

    return;
}

void AutoPicker::extractHelicalTubes(
    std::vector<ccfPeak> &peak_list,
    std::vector<std::vector<ccfPeak>> &tube_coord_list,
    std::vector<RFLOAT> &tube_len_list,
    std::vector<std::vector<ccfPeak>> &tube_track_list,
    RFLOAT particle_diameter_pix, RFLOAT curvature_factor_max,
    RFLOAT interbox_distance_pix, RFLOAT tube_diameter_pix,
    float scale
) {

    tube_coord_list.clear();
    tube_len_list.clear();
    tube_track_list.clear();

    // Rescaling
    particle_diameter_pix *= scale;
    interbox_distance_pix *= scale;
    tube_diameter_pix     *= scale;

    if (particle_diameter_pix < 5.0 * scale)
        REPORT_ERROR("autopicker.cpp::extractHelicalTubes: Particle diameter should be larger than 5 pixels!");
    if (curvature_factor_max < 0.0001 || curvature_factor_max > 1.0001)
        REPORT_ERROR("autopicker.cpp::extractHelicalTubes: Factor of curvature should be 0~1!");
    if (interbox_distance_pix < 0.9999 || interbox_distance_pix > particle_diameter_pix)
        REPORT_ERROR("autopicker.cpp::extractHelicalTubes: Interbox distance should be > 1 pixel and < particle diameter!");
    if (tube_diameter_pix < 1.0 || tube_diameter_pix > particle_diameter_pix)
        REPORT_ERROR("autopicker.cpp::extractHelicalTubes: Tube diameter should be > 1 pixel and < particle diameter!");
    if (peak_list.size() < 5) return;

    // Calculate the maximum curvature
    RFLOAT curvature_max = 2.0 * curvature_factor_max / particle_diameter_pix;
    //curvature_max = (sqrt(1. / scale)) * curvature_factor_max / (particle_diameter_pix / 2.);

    // Sort the peaks from the weakest to the strongest
    std::sort(peak_list.begin(), peak_list.end());

    std::vector<int> is_peak_on_other_tubes (peak_list.size());
    std::vector<int> is_peak_on_this_tube   (peak_list.size());
    for (int peak_id0 = 0; peak_id0 < is_peak_on_other_tubes.size(); peak_id0++)
        is_peak_on_other_tubes[peak_id0] = is_peak_on_this_tube[peak_id0] = -1;

    // Traverse peaks from the strongest to the weakest
    int tube_id = 0;
    RFLOAT rmax2 = particle_diameter_pix * particle_diameter_pix / 4.0;
    for (int peak_id0 = peak_list.size() - 1; peak_id0 >= 0; peak_id0--) {

        // Check whether this peak is included on other tubes
        if (is_peak_on_other_tubes[peak_id0] > 0) continue;

        // Probably a new tube
        tube_id++;
        is_peak_on_other_tubes[peak_id0] = tube_id;
        for (int peak_id1 = 0; peak_id1 < peak_list.size(); peak_id1++)
            is_peak_on_this_tube[peak_id1] = -1;
        is_peak_on_this_tube[peak_id0] = tube_id;

        // Gather all neighboring peaks around
        std::vector<ccfPeak> selected_peaks;
        for (int peak_id1 = 0; peak_id1 < peak_list.size(); peak_id1++) {
            if (peak_id0 == peak_id1) continue;
            if (is_peak_on_other_tubes[peak_id1] > 0) continue;
            const ccfPeak &peak0 = peak_list[peak_id0];
            const ccfPeak &peak1 = peak_list[peak_id1];

            RFLOAT dx = peak1.x - peak0.x;
            RFLOAT dy = peak1.y - peak0.y;
            RFLOAT dist2 = dx * dx + dy * dy;
            if (dist2 < rmax2) {
                ccfPeak myPeak = peak1;
                myPeak.dist = sqrt(dist2);
                myPeak.psi = degrees(safer_atan2(dy, dx));
                selected_peaks.push_back(myPeak);
            }
        }

        // 29 Sep 2015 ????????????
        // If fewer than 3 neighboring peaks are found, this is not a peak along a helical tube!
        if (selected_peaks.size() <= 2) continue;

        // This peak has >=2 neighboring peaks! Try to find an orientation!
        RFLOAT local_psi, local_dev, best_local_psi, best_local_dev, dev0, dev1, dev_weights;
        RFLOAT local_psi_sampling = 0.1;
        std::vector<ccfPeak> helical_track_dir1, helical_track_dir2, helical_track, helical_segments;
        RFLOAT len_dir1, len_dir2;

        // Find the averaged psi
        best_local_psi = -1.0;
        best_local_dev = 1e30;
        // Traverse every possible value of local_psi and calculate the dev
        for (local_psi = 0.0; local_psi < 180.0; local_psi += local_psi_sampling) {
            local_dev = 0.0;
            dev_weights = 0.0;
            for (int peak_id1 = 0; peak_id1 < selected_peaks.size(); peak_id1++) {
                dev0 = abs(selected_peaks[peak_id1].psi - local_psi);
                if (dev0 > 180.0) { dev0 = abs(dev0 - 360.0); }
                if (dev0 >  90.0) { dev0 = abs(dev0 - 180.0); }

                RFLOAT pixel_count = selected_peaks[peak_id1].nr_peak_pixel;
                if (pixel_count < 1.0) { pixel_count = 1.0; }
                local_dev += dev0 * pixel_count;
                dev_weights += pixel_count;
            }
            local_dev /= dev_weights;

            // Refresh if a better local psi is found
            if (local_dev < best_local_dev) {
                best_local_psi = local_psi;
                best_local_dev = local_dev;
            }
        }

        std::vector<ccfPeak> selected_peaks_dir1, selected_peaks_dir2;
        // Sort all peaks into dir1, dir2 and others
        RFLOAT psi_dir1 = 0.0, psi_dir2 = 0.0;
        for (const ccfPeak &peak1 : selected_peaks) {
            dev0 = dev1 = abs(peak1.psi - best_local_psi);
            if (dev1 > 180.0) { dev1 = abs(dev1 - 360.0); }
            if (dev1 >  90.0) { dev1 = abs(dev1 - 180.0); }
            RFLOAT curvature1 = radians(dev1) / peak1.dist;

            // Cannot fall into the estimated direction
            if (curvature1 > curvature_max) continue;

            // Psi direction or the opposite direction
            if (fabs(dev1 - dev0) < 0.1) {
                selected_peaks_dir2.push_back(peak1);
                psi_dir2 += peak1.psi;
            } else {
                selected_peaks_dir1.push_back(peak1);
                psi_dir1 += peak1.psi;
            }
        }

        RFLOAT xc, yc, xc_new, yc_new, xc_old, yc_old, dist_max, nr_psi_within_range;

        //std::cout << " nr Dir1 peaks= " << selected_peaks_dir1.size() << std::endl;
        // Dir1
        if (selected_peaks_dir1.size() >= 1) {
            // Init
            psi_dir1 /= selected_peaks_dir1.size();
            dist_max = -1.0;
            for (int peak_id1 = 0; peak_id1 < selected_peaks_dir1.size(); peak_id1++) {
                if (selected_peaks_dir1[peak_id1].dist > dist_max)
                    dist_max = selected_peaks_dir1[peak_id1].dist;
            }
            len_dir1 = 0.0;
            xc_old = peak_list[peak_id0].x;
            yc_old = peak_list[peak_id0].y;
            helical_track_dir1.clear();

            while (true) {
                // A new center along helical track dir1 is found, record it
                xc_new = xc_old + dist_max * cos(radians(psi_dir1));
                yc_new = yc_old + dist_max * sin(radians(psi_dir1));
                len_dir1 += dist_max;

                ccfPeak myPeak;
                myPeak.x = xc_new;
                myPeak.y = yc_new;
                myPeak.psi = psi_dir1;
                helical_track_dir1.push_back(myPeak);
                // std::cout << " Dir1 new center: x, y, psi= " << xc << ", " << yc << ", " << psi_dir1 << std::endl;
                // TODO: other parameters to add?

                // TODO: mark peaks along helical tracks
                xc = (xc_old + xc_new) / 2.0;
                yc = (yc_old + yc_new) / 2.0;
                rmax2 = (dist_max + tube_diameter_pix) * (dist_max + tube_diameter_pix) / 4.0;
                bool is_new_peak_found = false;
                bool is_combined_with_another_tube = true;
                for (int peak_id1 = 0; peak_id1 < peak_list.size(); peak_id1++) {

                    RFLOAT dx = peak_list[peak_id1].x - xc;
                    RFLOAT dy = peak_list[peak_id1].y - yc;
                    RFLOAT dist2 = dx * dx + dy * dy;

                    if (dist2 > rmax2)
                        continue;

                    RFLOAT psi = safer_atan2(dy, dx) - radians(psi_dir1);
                    RFLOAT dist = sqrt(dist2);
                    RFLOAT h = dist * fabs(cos(psi));
                    RFLOAT r = dist * fabs(sin(psi));

                    if (h < (dist_max + tube_diameter_pix) / 2.0 && r < tube_diameter_pix / 2.0) {
                        if (is_peak_on_this_tube[peak_id1] < 0) {
                            is_new_peak_found = true;
                            is_peak_on_this_tube[peak_id1] = tube_id;
                            if (is_peak_on_other_tubes[peak_id1] < 0) {
                                is_combined_with_another_tube = false;
                                is_peak_on_other_tubes[peak_id1] = tube_id;
                            }
                        }
                    }
                }
                if (!is_new_peak_found || is_combined_with_another_tube) {
                    // TODO: delete the end of this track list or not?
                    //helical_track_dir1.pop_back();
                    break;
                }

                // TODO: try to find another new center if possible
                xc_old = xc_new;
                yc_old = yc_new;
                rmax2 = particle_diameter_pix * particle_diameter_pix / 4.0;
                selected_peaks_dir1.clear();
                for (int peak_id1 = 0; peak_id1 < peak_list.size(); peak_id1++) {
                    if (is_peak_on_this_tube[peak_id1] > 0)
                        continue;

                    RFLOAT dx = peak_list[peak_id1].x - xc_old;
                    RFLOAT dy = peak_list[peak_id1].y - yc_old;
                    RFLOAT dist2 = dx * dx + dy * dy;
                    if (dist2 < rmax2) {
                        myPeak = peak_list[peak_id1];
                        myPeak.dist = sqrt(dist2);
                        myPeak.psi = degrees(safer_atan2(dy, dx));
                        selected_peaks_dir1.push_back(myPeak);
                    }
                }

                dist_max = -1.0;
                RFLOAT psi_sum = 0.0;
                RFLOAT psi_weights = 0.0;
                nr_psi_within_range = 0.0;
                int id_peak_dist_max;
                for (int peak_id1 = 0; peak_id1 < selected_peaks_dir1.size(); peak_id1++) {
                    // std::cout << "  Peak id " << selected_peaks_dir1[ii].id << " x, y, r, psi, psidir1= " << selected_peaks_dir1[ii].x << ", " << selected_peaks_dir1[ii].y
                    //     << ", " << selected_peaks_dir1[ii].r << ", " << selected_peaks_dir1[ii].psi << ", " << psi_dir1 << std::endl;

                    RFLOAT curvature = radians(abs(selected_peaks_dir1[peak_id1].psi - psi_dir1)) / selected_peaks_dir1[peak_id1].dist;
                    if (curvature < curvature_max) {
                        nr_psi_within_range += 1.0;

                        RFLOAT pixel_count = (RFLOAT) selected_peaks_dir1[peak_id1].nr_peak_pixel;
                        if (pixel_count < 1.0) { pixel_count = 1.0; }
                        psi_sum += selected_peaks_dir1[peak_id1].psi * pixel_count;
                        psi_weights += pixel_count;

                        if (selected_peaks_dir1[peak_id1].dist > dist_max) {
                            dist_max = selected_peaks_dir1[peak_id1].dist;
                            id_peak_dist_max = peak_id1;
                        }
                    }
                }

                // If no peaks are found in this round, the helical track stops, exit
                if (nr_psi_within_range < 0.5)
                    break;
                psi_dir1 = psi_sum / psi_weights;
            }
        }

        //std::cout << " nr Dir2 peaks= " << selected_peaks_dir2.size() << std::endl;
        // Dir2
        // ================================================================================================
        if (!selected_peaks_dir2.empty()) {
            // Init
            psi_dir2 /= selected_peaks_dir2.size();
            dist_max = std::max_element(
                selected_peaks_dir2.begin(), selected_peaks_dir2.end(),
                [] (ccfPeak lhs, ccfPeak rhs) { return lhs.dist < rhs.dist; }
            )->dist;
            len_dir2 = 0.0;
            xc_old = peak_list[peak_id0].x;
            yc_old = peak_list[peak_id0].y;
            helical_track_dir2.clear();

            while (true) {
                // A new center along helical track dir1 is found, record it
                xc_new = xc_old + dist_max * cos(radians(psi_dir2));
                yc_new = yc_old + dist_max * sin(radians(psi_dir2));
                len_dir2 += dist_max;

                ccfPeak myPeak;
                myPeak.x = xc_new;
                myPeak.y = yc_new;
                myPeak.psi = psi_dir2;
                helical_track_dir2.push_back(myPeak);
                // std::cout << " Dir1 new center: x, y, psi= " << xc << ", " << yc << ", " << psi_dir1 << std::endl;
                // TODO: other parameters to add?

                // TODO: mark peaks along helical tracks
                xc = xc_old + xc_new / 2.0;
                yc = yc_old + yc_new / 2.0;
                rmax2 = (dist_max + tube_diameter_pix) * (dist_max + tube_diameter_pix) / 4.0;
                bool is_new_peak_found = false;
                bool is_combined_with_another_tube = true;
                for (int peak_id1 = 0; peak_id1 < peak_list.size(); peak_id1++) {
                    RFLOAT dx = peak_list[peak_id1].x - xc;
                    RFLOAT dy = peak_list[peak_id1].y - yc;
                    RFLOAT dist2 = dx * dx + dy * dy;

                    if (dist2 > rmax2) continue;

                    RFLOAT dpsi = degrees(safer_atan2(dy, dx)) - psi_dir2;
                    RFLOAT dist = sqrt(dist2);
                    RFLOAT h = dist * fabs(cos(radians(dpsi)));
                    RFLOAT r = dist * fabs(sin(radians(dpsi)));

                    if (h < (dist_max + tube_diameter_pix) / 2.0 && r < tube_diameter_pix / 2.0) {
                        if (is_peak_on_this_tube[peak_id1] < 0) {
                            is_new_peak_found = true;
                            is_peak_on_this_tube[peak_id1] = tube_id;
                            if (is_peak_on_other_tubes[peak_id1] < 0) {
                                is_combined_with_another_tube = false;
                                is_peak_on_other_tubes[peak_id1] = tube_id;
                            }
                        }
                    }
                }
                if (is_new_peak_found || is_combined_with_another_tube) {
                    // TODO: delete the end of this track list or not?
                    break;
                }

                // TODO: try to find another new center if possible
                xc_old = xc_new;
                yc_old = yc_new;
                rmax2 = particle_diameter_pix * particle_diameter_pix / 4.0;
                selected_peaks_dir2.clear();
                for (int peak_id1 = 0; peak_id1 < peak_list.size(); peak_id1++) {
                    if (is_peak_on_this_tube[peak_id1] > 0) continue;

                    RFLOAT dx = peak_list[peak_id1].x - xc_old;
                    RFLOAT dy = peak_list[peak_id1].y - yc_old;
                    RFLOAT dist2 = dx * dx + dy * dy;
                    if (dist2 < rmax2) {
                        myPeak = peak_list[peak_id1];
                        myPeak.dist = sqrt(dist2);
                        myPeak.psi = degrees(safer_atan2(dy, dx));
                        selected_peaks_dir2.push_back(myPeak);
                    }
                }

                dist_max = -1.0;
                RFLOAT psi_sum = 0.0;
                RFLOAT psi_weights = 0.0;
                nr_psi_within_range = 0.0;
                int id_peak_dist_max;
                for (int peak_id1 = 0; peak_id1 < selected_peaks_dir2.size(); peak_id1++) {
                    // std::cout << "  Peak id " << selected_peaks_dir2[ii].id << " x, y, r, psi, psidir2= " << selected_peaks_dir2[ii].x << ", " << selected_peaks_dir2[ii].y
                    // 		<< ", " << selected_peaks_dir2[ii].r << ", " << selected_peaks_dir2[ii].psi << ", " << psi_dir2 << std::endl;

                    RFLOAT curvature = radians(abs(selected_peaks_dir2[peak_id1].psi - psi_dir2)) / selected_peaks_dir2[peak_id1].dist;
                    if (curvature < curvature_max) {
                        nr_psi_within_range += 1.0;

                        RFLOAT pixel_count = (RFLOAT) selected_peaks_dir2[peak_id1].nr_peak_pixel;
                        if (pixel_count < 1.0) { pixel_count = 1.0; }
                        psi_sum += selected_peaks_dir2[peak_id1].psi * pixel_count;
                        psi_weights += pixel_count;

                        if (selected_peaks_dir2[peak_id1].dist > dist_max) {
                            dist_max = selected_peaks_dir2[peak_id1].dist;
                            id_peak_dist_max = peak_id1;
                        }
                    }
                }

                // If no peaks are found in this round, the helical track stops, exit
                if (nr_psi_within_range < 0.5) break;
                psi_dir2 = psi_sum / psi_weights;
            }
        }

        // Get a full track
        helical_track.clear();

        for (
            // Iterate backwards through helical_track_dir2
            auto rit  = helical_track_dir2.rbegin(); 
                 rit != helical_track_dir2.rend(); ++rit
        ) { helical_track.push_back(*rit); }

        helical_track.push_back(peak_list[peak_id0]);

        for (
            // Iterate forwards through helical_track_dir1
            auto it  = helical_track_dir1.begin();
                 it != helical_track_dir1.end(); ++it
        ) { helical_track.push_back(*it); }

        // TODO: check below !!!
        if (
            len_dir1 + len_dir2 < particle_diameter_pix ||
            len_dir1 + len_dir2 < interbox_distance_pix ||
            helical_track.size() < 3
        ) {
            helical_track.clear();
        } else {
            RFLOAT dist_left, len_total;

            helical_segments.clear();

            // Get the first segment
            ccfPeak newSegment;
            newSegment.x = helical_track[0].x;
            newSegment.y = helical_track[0].y;
            newSegment.psi = degrees(atan2(helical_track[1].y - helical_track[0].y, helical_track[1].x - helical_track[0].x));
            newSegment.ref = helical_track[0].ref;
            helical_segments.push_back(newSegment);

            // Get segments along the track
            dist_left = 0.0;
            for (int inext = 1; inext < helical_track.size(); inext++) {

                RFLOAT x0 = helical_track[inext - 1].x;
                RFLOAT y0 = helical_track[inext - 1].y;
                RFLOAT dx = helical_track[inext].x - helical_track[inext - 1].x;
                RFLOAT dy = helical_track[inext].y - helical_track[inext - 1].y;
                RFLOAT psi = degrees(atan2(dy, dx));
                RFLOAT dist_total = sqrt(dx * dx + dy * dy);

                RFLOAT nr_segments_float = (dist_left + dist_total) / interbox_distance_pix;
                int nr_segments_int = floor(nr_segments_float);
                if (nr_segments_int >= 1) {
                    for (int iseg = 1; iseg <= nr_segments_int; iseg++) {
                        RFLOAT dist = (RFLOAT) iseg * interbox_distance_pix - dist_left;
                        dx = dist * cos(radians(psi));
                        dy = dist * sin(radians(psi));

                        newSegment.x = x0 + dx;
                        newSegment.y = y0 + dy;
                        newSegment.psi = psi;
                        newSegment.ref = helical_track[iseg * 2 < nr_segments_int ? inext - 1 : inext].ref;
                        helical_segments.push_back(newSegment);
                    }
                }

                dist_left = dist_left + dist_total - (RFLOAT) nr_segments_int * interbox_distance_pix;
            }

            // Get the last segment and mark it as invalid (different from what I did for the first segment)
            const int last_id = helical_track.size() - 1;
            newSegment.x = helical_track[last_id].x;
            newSegment.y = helical_track[last_id].y;
            newSegment.psi = 1e30;
            newSegment.ref = helical_track[last_id].ref;
            helical_segments.push_back(newSegment);

            len_total = len_dir1 + len_dir2;
            tube_coord_list.push_back(helical_segments);
            tube_len_list.push_back(len_total);
            tube_track_list.push_back(helical_track);

            // DEBUG
            #ifdef DEBUG_HELIX
            for (const auto ccfPeak &peak : helical_track)
                std::cout << "Track point x, y, psi= " << peak.x << ", " << peak.y << ", " << peak.psi << std::endl;
            std::cout << " Track length= " << len_dir1 + len_dir2 << std::endl;
            #endif
        }
    }

    return;
}

void AutoPicker::exportHelicalTubes(
    const MultidimArray<RFLOAT> &Mccf,
    MultidimArray<RFLOAT> &Mccfplot,
    const MultidimArray<int> &Mclass,
    std::vector<std::vector<ccfPeak>> &tube_coord_list,
    std::vector<std::vector<ccfPeak>> &tube_track_list,
    std::vector<RFLOAT> &tube_len_list,
    FileName &fn_mic_in,
    FileName &fn_star_out,
    RFLOAT particle_diameter_pix,
    RFLOAT tube_length_min_pix,
    int skip_side, float scale
) {
    // Rescale particle_diameter_pix, tube_length_min_pix, skip_side
    tube_length_min_pix *= scale;
    particle_diameter_pix *= scale;
    skip_side = (float) skip_side * scale;

    if (
        tube_coord_list.size() != tube_track_list.size() ||
        tube_track_list.size() != tube_len_list.size()
    ) {
        REPORT_ERROR("autopicker.cpp::exportHelicalTubes: BUG tube_coord_list.size() != tube_track_list.size() != tube_len_list.size()");
    }
    if (
        Yinit(Mccf) != Xmipp::init(Ysize(Mccf)) ||
        Xinit(Mccf) != Xmipp::init(Xsize(Mccf))
    ) {
        REPORT_ERROR("autopicker.cpp::exportHelicalTubes: The origin of input 3D MultidimArray is not at the center (use v.setXmippOrigin() before calling this function)!");
    }
    if (particle_diameter_pix < 5.0) // TODO: 5?
        REPORT_ERROR("autopicker.cpp::exportHelicalTubes: Particle diameter should be larger than 5 pixels!");

    // Mark tracks on Mccfplot
    Mccfplot.setXmippOrigin();
    for (const auto &track : tube_track_list) {
        for (int i = 1; i < track.size(); i++) {

            const ccfPeak &peak0 = track[i - 1];
            const ccfPeak &peak1 = track[i];

            RFLOAT x0 = peak0.x; RFLOAT y0 = peak0.y;
            RFLOAT x1 = peak1.x; RFLOAT y1 = peak1.y;
            RFLOAT dx = x1 - x0; RFLOAT dy = y1 - y0;

            RFLOAT psi = safer_atan2(dy, dx);
            RFLOAT dist_total = sqrt(dx * dx + dy * dy);

            if (dist_total < 2.0) continue;

            for (RFLOAT fdist = 1.0; fdist < dist_total; fdist += 1.0) {
                dx = fdist * cos(psi);
                dy = fdist * sin(psi);
                x1 = x0 + dx;
                y1 = y0 + dy;
                int x_int = round(x1);
                int y_int = round(y1);

                if (
                    x_int >= Xmipp::init(micrograph_xsize) + 1 &&
                    x_int <= Xmipp::last(micrograph_xsize) - 1 &&
                    y_int >= Xmipp::init(micrograph_ysize) + 1 &&
                    y_int <= Xmipp::last(micrograph_ysize) - 1
                ) {
                    Mccfplot.elem(x_int, y_int) = 1.0;
                }
            }
        }
    }

    // Detect crossovers
    RFLOAT dist2_min = particle_diameter_pix * particle_diameter_pix / 4.0;
    for (int itube1 = 0; itube1 + 1 < tube_coord_list.size(); itube1++)
    for (int icoord1 = 0; icoord1 < tube_coord_list[itube1].size(); icoord1++) {
    // Coord1 selected
    for (int itube2 = itube1 + 1; itube2 < tube_coord_list.size(); itube2++)
    for (int icoord2 = 0; icoord2 < tube_coord_list[itube2].size(); icoord2++) {
    // Coord2 selected

        ccfPeak &peak1 = tube_coord_list[itube1][icoord1];
        ccfPeak &peak2 = tube_coord_list[itube1][icoord2];

        RFLOAT x1 = peak1.x;
        RFLOAT y1 = peak1.y;
        RFLOAT x2 = peak2.x;
        RFLOAT y2 = peak2.y;
        RFLOAT dist2 = (x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1);

        // If this point is around the crossover
        if (dist2 < dist2_min) { peak1.psi = peak2.psi = 1e30; }

    }
    }

    // Cancel segments close to the ends of tubes
    /*
    for (int itube = 0; itube < tube_coord_list.size(); itube++) {
        if (tube_track_list[itube].size() < 2)
            continue;

        RFLOAT x_start, y_start, x_end, y_end, particle_radius_pix2;
        int last_id;

        last_id = tube_track_list[itube].size();
        last_id -= 1;

        x_start = tube_track_list[itube][0].x;
        y_start = tube_track_list[itube][0].y;
        x_end = tube_track_list[itube][last_id].x;
        y_end = tube_track_list[itube][last_id].y;
        particle_radius_pix2 = particle_diameter_pix * particle_diameter_pix / 4.0;

        for (int icoord = 0; icoord < tube_coord_list[itube].size(); icoord++)
        {
            if (fabs(tube_coord_list[itube][icoord].psi) > 360.)
                continue;

            RFLOAT x, y, dx1, dy1, dx2, dy2, dist21, dist22;

            x = tube_coord_list[itube][icoord].x;
            y = tube_coord_list[itube][icoord].y;
            dx1 = x - x_start;
            dy1 = y - y_start;
            dx2 = x - x_end;
            dy2 = y - y_end;
            dist21 = dx1 * dx1 + dy1 * dy1;
            dist22 = dx2 * dx2 + dy2 * dy2;

            if ( (dist21 < particle_radius_pix2) || (dist22 < particle_radius_pix2) )
                tube_coord_list[itube][icoord].psi = (1e30);
        }
    }
    */

    // Write out a STAR file with the coordinates
    MetaDataTable MDout;
    int helical_tube_id;
    RFLOAT helical_tube_len;

    // Only output STAR header if there are no tubes...
    MDout.clear();
    MDout.addLabel(EMDL::IMAGE_COORD_X);
    MDout.addLabel(EMDL::IMAGE_COORD_Y);
    MDout.addLabel(EMDL::PARTICLE_CLASS);
    MDout.addLabel(EMDL::PARTICLE_AUTOPICK_FOM);
    MDout.addLabel(EMDL::PARTICLE_HELICAL_TUBE_ID);
    MDout.addLabel(EMDL::ORIENT_TILT_PRIOR);
    MDout.addLabel(EMDL::ORIENT_PSI_PRIOR);
    MDout.addLabel(EMDL::PARTICLE_HELICAL_TRACK_LENGTH_ANGSTROM);
    MDout.addLabel(EMDL::ORIENT_PSI_PRIOR_FLIP_RATIO);
    MDout.addLabel(EMDL::ORIENT_ROT_PRIOR_FLIP_RATIO);  //KThurber

    helical_tube_id = 0;
    for (int itube = 0; itube < tube_coord_list.size(); itube++) {
        if (
            tube_length_min_pix > particle_diameter_pix && 
            tube_len_list[itube] < tube_length_min_pix
        ) continue;
        helical_tube_id++;
        helical_tube_len = 0.0;
        for (int icoord = 0; icoord < tube_coord_list[itube].size(); icoord++) {

            if (icoord > 0) {
                RFLOAT dx = (RFLOAT) tube_coord_list[itube][icoord].x - (RFLOAT) tube_coord_list[itube][icoord - 1].x;
                RFLOAT dy = (RFLOAT) tube_coord_list[itube][icoord].y - (RFLOAT) tube_coord_list[itube][icoord - 1].y;
                helical_tube_len += sqrt(dx * dx + dy * dy);
            }

            // Invalid psi (crossover)
            if (fabs(tube_coord_list[itube][icoord].psi) > 360.0)
                continue;

            int x_int = round(tube_coord_list[itube][icoord].x);
            int y_int = round(tube_coord_list[itube][icoord].y);

            // Out of range
            if (
                x_int < Xmipp::init(micrograph_xsize) + skip_side + 1 ||
                x_int > Xmipp::last(micrograph_xsize) - skip_side - 1 ||
                y_int < Xmipp::init(micrograph_ysize) + skip_side + 1 ||
                y_int > Xmipp::last(micrograph_ysize) - skip_side - 1
            ) continue;

            int iref = Mclass.elem(x_int, y_int);
            RFLOAT fom = Mccf.elem(x_int, y_int);

            const long int i = MDout.addObject();
            const RFLOAT xval = tube_coord_list[itube][icoord].x / scale - (RFLOAT) Xmipp::init(micrograph_xsize);
            const RFLOAT yval = tube_coord_list[itube][icoord].y / scale - (RFLOAT) Xmipp::init(micrograph_ysize);
            MDout.setValue(EMDL::IMAGE_COORD_X, xval, i);
            MDout.setValue(EMDL::IMAGE_COORD_Y, yval, i);
            MDout.setValue(EMDL::PARTICLE_CLASS, iref + 1, i); // start counting at 1
            MDout.setValue(EMDL::PARTICLE_AUTOPICK_FOM, fom, i);
            MDout.setValue(EMDL::PARTICLE_HELICAL_TUBE_ID, helical_tube_id, i);
            MDout.setValue(EMDL::ORIENT_TILT_PRIOR, 90.0, i);
            MDout.setValue(EMDL::ORIENT_PSI_PRIOR, -tube_coord_list[itube][icoord].psi, i); // Beware! Multiplied by -1!
            MDout.setValue(EMDL::PARTICLE_HELICAL_TRACK_LENGTH_ANGSTROM, angpix * helical_tube_len, i);
            MDout.setValue(EMDL::ORIENT_PSI_PRIOR_FLIP_RATIO, BIMODAL_PSI_PRIOR_FLIP_RATIO, i);
            MDout.setValue(EMDL::ORIENT_ROT_PRIOR_FLIP_RATIO, BIMODAL_PSI_PRIOR_FLIP_RATIO, i);	// KThurber
        }
    }

    FileName fn = getOutputRootName(fn_mic_in) + "_" + fn_star_out + ".star";
    MDout.write(fn);

    return;
}

void AutoPicker::autoPickLoGOneMicrograph(const FileName &fn_mic, long int imic) {
    FourierTransformer transformer;
    MultidimArray<float> Mbest_size, Mbest_fom;
    float scale = (float) workSize / (float) micrograph_size;

    Mbest_size.resize(workSize, workSize);
    (Mbest_size = -999.0).setXmippOrigin();
    Mbest_fom.resize(workSize, workSize);
    (Mbest_fom = -999.0).setXmippOrigin();

    if (!do_read_fom_maps) {
        // Always use the same random seed
        init_random_generator(random_seed + imic);

        // Read in the micrograph
        auto Imic = Image<RFLOAT>::from_filename(fn_mic);
        Imic().setXmippOrigin();

        // Let's just check the square size again...
        RFLOAT my_xsize = Xsize(Imic());
        RFLOAT my_ysize = Ysize(Imic());
        RFLOAT my_size = std::max(my_xsize, my_ysize);

        if (
            my_xsize != micrograph_xsize ||
            my_ysize != micrograph_ysize ||
            my_size  != micrograph_size
        ) {
            Imic().printShape();
            std::cerr << " micrograph_size= " << micrograph_size << " micrograph_xsize= " << micrograph_xsize << " micrograph_ysize= " << micrograph_ysize << std::endl;
            REPORT_ERROR("AutoPicker::autoPickOneMicrograph ERROR: No differently sized micrographs are allowed in one run, sorry you will have to run separately for each size...");
        }

        // Set mean to zero and stddev to 1 to prevent numerical problems with one-sweep stddev calculations.
        const auto stats = computeStats(Imic());

        for (RFLOAT &x : Imic()) {
            // Values that are too far from the mean are set to the mean (0)
            auto z = (x - stats.avg) / stats.stddev;
            if (abs(z) > outlier_removal_zscore) { x = stats.avg; }

            x = (x - stats.avg) / stats.stddev;
        }

        // Have positive LoG maps
        if (!LoG_invert) { Imic() *= -1.0; }

        if (
            micrograph_xsize != micrograph_size ||
            micrograph_ysize != micrograph_size
        ) {
            // Window non-square micrographs to be a square with the largest side
            rewindow(Imic, micrograph_size);

            // Fill region outside the original window with white Gaussian noise to prevent all-zeros in Mstddev
            FOR_ALL_ELEMENTS_IN_ARRAY2D(Imic(), i, j) {
                if (
                    i < Xmipp::init(micrograph_xsize) ||
                    i > Xmipp::last(micrograph_xsize) ||
                    j < Xmipp::init(micrograph_ysize) ||
                    j > Xmipp::last(micrograph_ysize)
                ) {
                    Imic().elem(i, j) = rnd_gaus(0.0, 1.0);
                }
            }
        }

        // Fourier Transform (and downscale) Imic()
        // Use downsized FFTs
        MultidimArray<Complex> Fmic = windowFourierTransform(
            transformer.FourierTransform(Imic()), workSize
        );

        if (LoG_use_ctf) {
            CTF ctf = find_micrograph_ctf(MDmic, fn_mic, obsModel);
            MultidimArray<RFLOAT> Fctf = CtfHelper::getFftwImage(
                ctf,
                Xsize(Fmic), Ysize(Fmic), micrograph_size, micrograph_size, angpix,
                &obsModel,
                false, false, false, false, false, true
            );
            Fmic /= Fctf;
        }

        Image<RFLOAT> Maux(workSize, workSize);

        // Maux() = transformer.inverseFourierTransform(Fmic);
        // Maux.write("LoG-ctf-filtered.mrc");
        // REPORT_ERROR("stop");

        // Make the diameter of the LoG filter larger in steps of LoG_incr_search (=1.5)
        // Search sizes from LoG_min_diameter to LoG_max_search (=5) * LoG_max_diameter
        for (const RFLOAT &d : diams_LoG) {

            MultidimArray<Complex> Faux = Fmic;
            LoGFilterMap(Faux, micrograph_size, d, angpix);
            Maux() = transformer.inverseFourierTransform(Faux);

            if (do_write_fom_maps) {
                FileName fn_LoG = getOutputRootName(fn_mic) + "_" + fn_out + "_LoG" + integerToString(round(d)) + ".spi";
                Maux.write(fn_LoG);
            }

            for (long int n = 0; n < Maux().size(); n++) {
                if (Maux()[n] > Mbest_fom[n]) {
                    Mbest_fom[n] = Maux()[n];
                    Mbest_size[n] = d;
                }
            }

        }

    } else {

        // Read back in pre-calculated LoG maps
        for (const RFLOAT &d : diams_LoG) {

            const FileName fn_LoG = getOutputRootName(fn_mic) + "_" + fn_out + "_LoG" + integerToString(round(d)) + ".spi";
            const auto LoG = Image<RFLOAT>::from_filename(fn_LoG)();

            for (long int n = 0; n < LoG.size(); n++) {
                if (LoG[n] > Mbest_fom[n]) {
                    Mbest_fom[n] = LoG[n];
                    Mbest_size[n] = d;
                }
            }
        }
    }

    if (do_write_fom_maps) {

        const FileName fn_bestLoG = getOutputRootName(fn_mic) + "_" + fn_out + "_bestLoG.spi";
        Image<float>(Mbest_fom).write(fn_bestLoG);

        const FileName fn_bestSize = getOutputRootName(fn_mic) + "_" + fn_out + "_bestSize.spi";
        Image<float>(Mbest_size).write(fn_bestSize);

    }

    // Skip the sides if necessary
    int my_skip_side = (float) autopick_skip_side * scale;
    if (my_skip_side > 0) {
        MultidimArray<float> Mbest_fom_new = MultidimArray<float>::zeros(Mbest_fom);
        for (int i = Xmipp::init((int) ((float) micrograph_ysize * scale)) + my_skip_side; i <= Xmipp::last((int) ((float) micrograph_ysize * scale)) - my_skip_side; i++)
        for (int j = Xmipp::init((int) ((float) micrograph_xsize * scale)) + my_skip_side; j <= Xmipp::last((int) ((float) micrograph_xsize * scale)) - my_skip_side; j++) {
            Mbest_fom_new.elem(i, j) = Mbest_fom.elem(i, j);
        }
        Mbest_fom = Mbest_fom_new;
    }

    // See which pixels have the best diameters within the desired diameter range
    // Also store average and stddev of FOMs outside that range, in order to idea of the noise in the FOMs
    RFLOAT sum_fom_low = 0.0;
    RFLOAT sum_fom_high = 0.0;
    RFLOAT sum_fom_ok = 0.0;
    RFLOAT sum2_fom_low = 0.0;
    RFLOAT sum2_fom_high = 0.0;
    RFLOAT sum2_fom_ok = 0.0;
    RFLOAT count_low = 0.0;
    RFLOAT count_high = 0.0;
    RFLOAT count_ok = 0.0;
    for (long int n = 0; n < Mbest_size.size(); n++) {
        if (Mbest_size[n] > LoG_max_diameter) {
            sum_fom_high += Mbest_fom[n];
            sum2_fom_high += Mbest_fom[n] * Mbest_fom[n];
            count_high += 1.0;
            Mbest_fom[n] = 0.0;
        } else if (Mbest_size[n] < LoG_min_diameter) {
            sum_fom_low += Mbest_fom[n];
            sum2_fom_low += Mbest_fom[n] * Mbest_fom[n];
            count_low += 1.0;
            Mbest_fom[n] = 0.0;
        } else {
            sum_fom_ok += Mbest_fom[n];
            sum2_fom_ok += Mbest_fom[n] * Mbest_fom[n];
            count_ok += 1.0;
        }
    }

    if (do_write_fom_maps) {
        Image<float> IMbest_fom (Mbest_fom);
        FileName fn_bestLoGb = getOutputRootName(fn_mic) + "_" + fn_out + "_bestLoGb.spi";
        IMbest_fom.write(fn_bestLoGb);
    }

    // Average of FOMs outside desired diameter range
    RFLOAT sum_fom_outside = (sum_fom_low + sum_fom_high) / (count_low + count_high);
    sum_fom_low /= count_low;
    sum_fom_high /= count_high;
    sum_fom_ok /= count_ok;
    // Variance of FOMs outside desired diameter range
    sum2_fom_low  = sum2_fom_low  / count_low  - sum_fom_low  * sum_fom_low;
    sum2_fom_high = sum2_fom_high / count_high - sum_fom_high * sum_fom_high;
    sum2_fom_ok   = sum2_fom_ok   / count_ok   - sum_fom_ok   * sum_fom_ok;
    //float my_threshold =  sum_fom_low + LoG_adjust_threshold * sqrt(sum2_fom_low);
    //Sjors 25May2018: better have threshold only depend on fom_ok, as in some cases fom_low/high are on very different scale...
    const float my_threshold   = sum_fom_ok + sqrt(sum2_fom_ok) * LoG_adjust_threshold;
    const float my_upper_limit = sum_fom_ok + sqrt(sum2_fom_ok) * LoG_upper_limit;

    #ifdef DEBUG_LOG
    std::cerr << " avg_fom_low= "  << sum_fom_low  << " stddev_fom_low= "  << sqrt(sum2_fom_low)  << " N= " << count_low  << std::endl;
    std::cerr << " avg_fom_high= " << sum_fom_high << " stddev_fom_high= " << sqrt(sum2_fom_high) << " N= " << count_high << std::endl;
    std::cerr << " avg_fom_ok= "   << sum_fom_ok   << " stddev_fom_ok= "   << sqrt(sum2_fom_ok)   << " N= " << count_ok   << std::endl;
    std::cerr << " avg_fom_outside= " << sum_fom_outside << std::endl;
    std::cerr << " my_threshold= " << my_threshold << " LoG_adjust_threshold= " << LoG_adjust_threshold << std::endl;
    #endif

    // Threshold the best_fom map
    for (auto &x : Mbest_fom) { if (x < my_threshold) { x = 0.0; } }

    if (do_write_fom_maps)
    Image<float>(Mbest_fom).write(getOutputRootName(fn_mic) + "_" + fn_out + "_bestLoGc.spi");

    // Now just start from the biggest peak: put a particle coordinate there, remove all neighbouring pixels within corresponding Mbest_size and loop
    MetaDataTable MDout;
    auto it = Mbest_fom.begin();
    while (*(it = std::max_element(Mbest_fom.begin(), Mbest_fom.end())) > 0.0) {
        const auto direct_indices = offset_to_direct_indices(Mbest_fom, it - Mbest_fom.begin());
        const auto Xmipp_indices = direct_indices_to_Xmipp_indices(Mbest_fom, direct_indices[0], direct_indices[1], direct_indices[2], direct_indices[3]);
        const long int imax = Xmipp_indices[0], jmax = Xmipp_indices[1];
        const RFLOAT fom = Mbest_fom.elem(imax, jmax);
        if (fom < my_upper_limit) {
            const long int i = MDout.addObject();
            const long int xx = imax - Xmipp::init((float) micrograph_xsize * scale);
            const long int yy = jmax - Xmipp::init((float) micrograph_ysize * scale);
            MDout.setValue(EMDL::IMAGE_COORD_X, (RFLOAT) xx / scale, i);
            MDout.setValue(EMDL::IMAGE_COORD_Y, (RFLOAT) yy / scale, i);
            MDout.setValue(EMDL::PARTICLE_AUTOPICK_FOM, Mbest_fom.elem(imax, jmax), i);
            MDout.setValue(EMDL::PARTICLE_CLASS, 0, i);  // Dummy values to avoid problems in JoinStar
            MDout.setValue(EMDL::ORIENT_PSI, 0.0, i);
        }

        // Now set all pixels of Mbest_fom within a distance of 0.5* the corresponding Mbest_size to zero
        // Exclude a bit more radius, such that no very close neighbours are allowed
        long int myrad = round(scale * (Mbest_size.elem(imax, jmax) + LoG_min_diameter) * LoG_neighbour_fudge / 2 / angpix);
        long int myrad2 = myrad * myrad;
        // std::cout << "scale = " << scale << " Mbest_size = " << Mbest_size.elem(imax, jmax) << " myrad " << myrad << std::endl;
        for (long int ii = imax - myrad; ii <= imax + myrad; ii++)
        for (long int jj = jmax - myrad; jj <= jmax + myrad; jj++) {
            long int r2 = (imax - ii) * (imax - ii) + (jmax - jj) * (jmax - jj);
            if (
                ii >= Xinit(Mbest_fom) && ii <= Xlast(Mbest_fom) &&
                jj >= Yinit(Mbest_fom) && jj <= Ylast(Mbest_fom) &&
                r2 < myrad2
            ) {
                Mbest_fom.elem(ii, jj) = 0.0;
            }
        }
    }

    if (verb > 1)
        std::cerr << "Picked " << MDout.size() << " of particles " << std::endl;
    FileName fn = getOutputRootName(fn_mic) + "_" + fn_out + ".star";
    MDout.write(fn);
}

void AutoPicker::autoPickOneMicrograph(FileName &fn_mic, long int imic) {
    Image<RFLOAT> Imic;
    MultidimArray<Complex> Faux2, Fmic;
    MultidimArray<RFLOAT> Maux, Mstddev, Mmean, Mstddev2, Mavg, Mdiff2, MsumX2, Mccf_best, Mpsi_best, Mccf_best_combined, Mpsi_best_combined;
    MultidimArray<int> Mclass_best_combined;
    FourierTransformer transformer;
    RFLOAT sum_ref_under_circ_mask, sum_ref2_under_circ_mask;
    int my_skip_side = autopick_skip_side + particle_size / 2;

    int min_distance_pix = round(min_particle_distance / angpix);
    float scale = (float) workSize / (float) micrograph_size;

    // Always use the same random seed
    init_random_generator(random_seed + imic);

    #ifdef DEBUG
    Image<RFLOAT> tt;
    tt().resize(micrograph_size, micrograph_size);
    std::cerr << " fn_mic= " << fn_mic << std::endl;
    #endif

    {
    ifdefTIMING(TicToc tt (timer, TIMING_A6);)
    // Read in the micrograph
    Imic.read(fn_mic);
    Imic().setXmippOrigin();
    }

    // Let's just check the square size again...
    RFLOAT my_xsize = Xsize(Imic());
    RFLOAT my_ysize = Ysize(Imic());
    RFLOAT my_size = std::max(my_xsize, my_ysize);
    if (extra_padding > 0) { my_size += 2 * extra_padding; }

    if (
        my_xsize != micrograph_xsize ||
        my_ysize != micrograph_ysize ||
        my_size != micrograph_size
    ) {
        Imic().printShape();
        std::cerr << " micrograph_size= " << micrograph_size << " micrograph_xsize= " << micrograph_xsize << " micrograph_ysize= " << micrograph_ysize << std::endl;
        REPORT_ERROR("AutoPicker::autoPickOneMicrograph ERROR: No differently sized micrographs are allowed in one run, sorry you will have to run separately for each size...");
    }

    {
    ifdefTIMING(TicToc tt (timer, TIMING_A7);)
    // Set mean to zero and stddev to 1 to prevent numerical problems with one-sweep stddev calculations.
    const auto stats = computeStats(Imic());

    for (auto &x : Imic()) {
        // Remove pixel values that are too far away from the mean
        auto z = (x - stats.avg) / stats.stddev;
        if (abs(z) > outlier_removal_zscore) { x = stats.avg; }

        x = (x - stats.avg) / stats.stddev;
    }

    if (
        micrograph_xsize != micrograph_size ||
        micrograph_ysize != micrograph_size
    ) {
        // Window non-square micrographs to be a square with the largest side
        rewindow(Imic, micrograph_size);

        // Fill region outside the original window with white Gaussian noise to prevent all-zeros in Mstddev
        FOR_ALL_ELEMENTS_IN_ARRAY2D(Imic(), i, j) {
            if (
                j < Xmipp::init(micrograph_ysize) ||
                j > Xmipp::last(micrograph_ysize) ||
                i < Xmipp::init(micrograph_xsize) ||
                i > Xmipp::last(micrograph_xsize)
            ) {
                Imic().elem(i, j) = rnd_gaus(0.0, 1.0);
            }
        }
    }
    }

    MultidimArray<RFLOAT> Fctf;
    {
    ifdefTIMING(TicToc tt (timer, TIMING_A8);)
    // Read in the CTF information if needed
    if (do_ctf) {
        CTF ctf = find_micrograph_ctf(MDmic, fn_mic, obsModel);
        Fctf.resize(downsize_mic, downsize_mic / 2 + 1);
        Fctf = CtfHelper::getFftwImage(
            ctf,
            downsize_mic, downsize_mic / 2 + 1, 
            micrograph_size, micrograph_size,
            angpix, &obsModel,
            false, false, intact_ctf_first_peak, true
        );

        #ifdef DEBUG
        std::cerr << " Read CTF info from" << fn_mic.withoutExtension() << "_ctf.star" << std::endl;
        Image<RFLOAT> Ictf (Fctf);
        Ictf.write("Mmic_ctf.spi");
        #endif
    }
    }

    {
    ifdefTIMING(TicToc tt (timer, TIMING_A9);)
    Mccf_best.resize(workSize, workSize);
    Mpsi_best.resize(workSize, workSize);
    }

    // Sjors 18 Apr 2016
    RFLOAT normfft = (RFLOAT) (micrograph_size * micrograph_size) / (RFLOAT) nr_pixels_circular_mask;
    {
    ifdefTIMING(TicToc tt (timer, TIMING_B1);)
    if (do_read_fom_maps) {

        FileName fn_stddev = getOutputRootName(fn_mic) + "_" + fn_out + "_stddevNoise.spi";
        Image<RFLOAT> I_stddev;
        I_stddev.read(fn_stddev);
        (autopick_helical_segments ? Mstddev2 : Mstddev) = I_stddev();

        Image<RFLOAT> I_avg;
        FileName fn_avg = getOutputRootName(fn_mic) + "_" + fn_out + "_avgNoise.spi";
        I_avg.read(fn_avg);
        (autopick_helical_segments ? Mavg : Mmean) = I_avg();

    } else {
        /*
         * Squared difference FOM:
         * Sum ( (X-mu)/sig  - A )^2 =
         *  = Sum((X-mu)/sig)^2 - 2 Sum (A*(X-mu)/sig) + Sum(A)^2
         *  = (1/sig^2)*Sum(X^2) - (2*mu/sig^2)*Sum(X) + (mu^2/sig^2)*Sum(1) - (2/sig)*Sum(AX) + (2*mu/sig)*Sum(A) + Sum(A^2)
         *
         * However, the squared difference with an "empty" ie all-zero reference is:
         * Sum ( (X-mu)/sig)^2
         *
         * The ratio of the probabilities thereby becomes:
         * P(ref) = 1/sqrt(2pi) * exp (( (X-mu)/sig  - A )^2 / -2 )   // assuming sigma = 1!
         * P(zero) = 1/sqrt(2pi) * exp (( (X-mu)/sig )^2 / -2 )
         *
         * P(ref)/P(zero) = exp(( (X-mu)/sig  - A )^2 / -2) / exp ( ( (X-mu)/sig )^2 / -2)
         *                = exp( (- (2/sig)*Sum(AX) + (2*mu/sig)*Sum(A) + Sum(A^2)) / - 2 )
         *
         *                Therefore, I do not need to calculate (X-mu)/sig beforehand!!!
         *
         */

        // Fourier Transform (and downscale) Imic()
        Fmic = transformer.FourierTransform(Imic());

        if (highpass > 0.0) {
            highPassFilterMap(Fmic, micrograph_size, highpass, angpix, 2);
            Imic() = transformer.inverseFourierTransform(Fmic); // also calculate inverse transform again for squared calculation below
        }

        CenterFFTbySign(Fmic);

        // Also calculate the FFT of the squared micrograph
        Maux.resize(micrograph_size, micrograph_size);
        Maux = Imic() * Imic();
        MultidimArray<Complex> Fmic2 = transformer.FourierTransform(Maux);
        CenterFFTbySign(Fmic2);

        Maux.resize(workSize,workSize);

        #ifdef DEBUG
        std::cerr << " nr_pixels_circular_invmask= " << nr_pixels_circular_invmask << std::endl;
        std::cerr << " nr_pixels_circular_mask= " << nr_pixels_circular_mask << std::endl;
        Faux2 = windowFourierTransform(Finvmsk, micrograph_size);
        CenterFFTbySign(Faux2);
        tt() = transformer.inverseFourierTransform(Faux2);
        tt.write("Minvmask.spi");
        #endif

        // The following calculate mu and sig under the solvent area at every position in the micrograph
        if (autopick_helical_segments)
            calculateStddevAndMeanUnderMask(Fmic, Fmic2, Favgmsk, nr_pixels_avg_mask, Mstddev2, Mavg);
        calculateStddevAndMeanUnderMask(Fmic, Fmic2, Finvmsk, nr_pixels_circular_invmask, Mstddev, Mmean);

        if (do_write_fom_maps) {
            FileName fn_stddev = getOutputRootName(fn_mic) + "_" + fn_out + "_stddevNoise.spi";
            Image<RFLOAT> It_stddev (autopick_helical_segments ? Mstddev2 : Mstddev);
            It_stddev.write(fn_stddev);

            FileName fn_avg = getOutputRootName(fn_mic) + "_" + fn_out + "_avgNoise.spi";
            Image<RFLOAT> It_avg (autopick_helical_segments ? Mavg : Mmean);
            It_avg.write(fn_avg);
        }

        // From now on use downsized Fmic, as the cross-correlation with the references can be done at lower resolution
        Fmic = windowFourierTransform(Fmic, downsize_mic);

    }
    }

    // Now start looking for the peaks of all references
    // Clear the output vector with all peaks
    std::vector<Peak> peaks;
    peaks.clear();

    if (autopick_helical_segments) {
        if (do_read_fom_maps) {

            FileName fn_combinedCCF = getOutputRootName(fn_mic) + "_" + fn_out + "_combinedCCF.spi";
            Image<RFLOAT> It_float;
            It_float.read(fn_combinedCCF);
            Mccf_best_combined = It_float();

            if (do_amyloid) {
                FileName fn_combinedPSI = getOutputRootName(fn_mic) + "_" + fn_out + "_combinedPSI.spi";
                Image<RFLOAT> It_float;
                It_float.read(fn_combinedPSI);
                Mpsi_best_combined = It_float();
            } else {
                FileName fn_combinedCLASS = getOutputRootName(fn_mic) + "_" + fn_out + "_combinedCLASS.spi";
                Image<int> It_int;
                It_int.read(fn_combinedCLASS);
                Mclass_best_combined = It_int();
            }
        } else {
            Mccf_best_combined.clear();
            Mccf_best_combined.resize(workSize, workSize);
            Mccf_best_combined = -99.0e99;
            Mpsi_best_combined.clear();
            Mpsi_best_combined.resize(workSize, workSize);
            Mpsi_best_combined = -99.0e99;
            Mclass_best_combined.clear();
            Mclass_best_combined.resize(workSize, workSize);
            Mclass_best_combined = -1.0;
        }
    }

    for (int iref = 0; iref < Mrefs.size(); iref++) {
        RFLOAT expected_Pratio; // the expectedFOM for this (ctf-corrected) reference
        if (do_read_fom_maps) {

            {
            ifdefTIMING(TicToc tt (timer, TIMING_B2);)
            if (!autopick_helical_segments) {
                const auto fn_bestCCF = FileName::compose(getOutputRootName(fn_mic) + "_" + fn_out + "_ref", iref, "_bestCCF.spi");
                Image<RFLOAT> I_bestCCF;
                I_bestCCF.read(fn_bestCCF);
                Mccf_best = I_bestCCF();
                expected_Pratio = I_bestCCF.header.getValue<RFLOAT>(EMDL::IMAGE_STATS_MAX, I_bestCCF.header.size() - 1);  // Retrieve expected_Pratio from the header of the image

                const auto fn_bestPSI = FileName::compose(getOutputRootName(fn_mic) + "_" + fn_out + "_ref", iref, "_bestPSI.spi");
                Mpsi_best = Image<RFLOAT>::from_filename(fn_bestPSI)();
            }
            }
        } else {
            {
            ifdefTIMING(TicToc tt (timer, TIMING_B3);)
            Mccf_best = -LARGE_NUMBER;
            bool is_first_psi = true;
            for (RFLOAT psi = 0.0; psi < 360.0; psi += psi_sampling) {
                // Get the Euler matrix
                const auto A = Euler::angles2matrix(0.0, 0.0, psi);

                // Now get the FT of the rotated (non-ctf-corrected) template
                auto Faux = PPref[iref].get2DFourierTransform(downsize_mic / 2 + 1, downsize_mic, 1, A);

                #ifdef DEBUG
                std::cerr << " psi= " << psi << std::endl;
                Faux2 = windowFourierTransform(Faux, micrograph_size);
                CenterFFTbySign(Faux2);
                tt().resize(micrograph_size, micrograph_size);
                tt() = transformer.inverseFourierTransform(Faux2);
                tt.write("Mref_rot.spi");

                Faux2 = windowFourierTransform(Fmic, micrograph_size);
                CenterFFTbySign(Faux2);
                tt() = transformer.inverseFourierTransform(Faux2);
                tt.write("Mmic.spi");
                #endif

                // Apply the CTF on-the-fly (so same PPref can be used for many different micrographs)
                if (do_ctf) {
                    {
                    ifdefTIMING(TicToc tt (timer, TIMING_B4);)
                    Faux *= Fctf;
                    }
                    #ifdef DEBUG
                    MultidimArray<RFLOAT> ttt(micrograph_size, micrograph_size);
                    Faux2 = windowFourierTransform(Faux, micrograph_size);
                    CenterFFTbySign(Faux2);
                    ttt = transformer.inverseFourierTransform(Faux);
                    ttt.setXmippOrigin();
                    tt().resize(particle_size, particle_size);
                    tt().setXmippOrigin();
                    FOR_ALL_ELEMENTS_IN_ARRAY2D(tt(), i, j) {
                        tt.elem(i, j) = ttt.elem(i, j);
                    }
                    tt.write("Mref_rot_ctf.spi");
                    #endif
                }

                if (is_first_psi) {
                    {
                    ifdefTIMING(TicToc tt (timer, TIMING_B5);)
                    // Calculate the expected ratio of probabilities for this CTF-corrected reference
                    // and the sum_ref_under_circ_mask and sum_ref_under_circ_mask2
                    // Do this also if we're not recalculating the fom maps...
                    // This calculation needs to be done on an "non-shrinked" micrograph, in order to get the correct I^2 statistics
                    Faux2 = windowFourierTransform(Faux, micrograph_size);
                    CenterFFTbySign(Faux2);
                    Maux.resize(micrograph_size, micrograph_size);
                    Maux = transformer.inverseFourierTransform(Faux2);
                    Maux.setXmippOrigin();
                    #ifdef DEBUG
                    Image<RFLOAT> ttt;
                    ttt() = Maux;
                    ttt.write("Maux.spi");
                    #endif
                    sum_ref_under_circ_mask = 0.0;
                    sum_ref2_under_circ_mask = 0.0;
                    RFLOAT suma2 = 0.0;
                    RFLOAT sumn = 1.0;
                    MultidimArray<RFLOAT> Mctfref(particle_size, particle_size);
                    Mctfref.setXmippOrigin();
                    FOR_ALL_ELEMENTS_IN_ARRAY2D(Mctfref, i, j) {
                        // only loop over smaller Mctfref, but take values from large Maux!
                        if (i * i + j * j < particle_radius2) {
                            suma2 += Maux.elem(i, j) * Maux.elem(i, j);
                            suma2 += 2.0 * Maux.elem(i, j) * rnd_gaus(0.0, 1.0);
                            sum_ref_under_circ_mask  += Maux.elem(i, j);
                            sum_ref2_under_circ_mask += Maux.elem(i, j) * Maux.elem(i, j);
                            sumn += 1.0;
                        }
                        #ifdef DEBUG
                        Mctfref.elem(i, j) = Maux.elem(i, j);
                        #endif
                    }
                    sum_ref_under_circ_mask  /= sumn;
                    sum_ref2_under_circ_mask /= sumn;
                    expected_Pratio = exp(suma2 / (2.0 * sumn));
                    #ifdef DEBUG
                    std::cerr << " expected_Pratio[" << iref << "]= " << expected_Pratio << std::endl;
                    tt() = Mctfref;
                    tt.write("Mctfref.spi");
                    std::cerr << "suma2 " << suma2 << " sumn " << sumn << " suma2/2sumn=" << suma2 / (2.0 * sumn) << std::endl;
                    std::cerr << " nr_pixels_under_mask= " << nr_pixels_circular_mask << " nr_pixels_under_invmask= " << nr_pixels_circular_invmask << std::endl;
                    std::cerr << "sum_ref_under_circ_mask " << sum_ref_under_circ_mask << std::endl;
                    std::cerr << "sum_ref2_under_circ_mask " << sum_ref2_under_circ_mask << std::endl;
                    std::cerr << "expected_Pratio " << expected_Pratio << std::endl;
                    #endif

                    // Maux goes back to the workSize
                    Maux.resize(workSize, workSize);
                    }
                }

                {
                ifdefTIMING(TicToc tt (timer, TIMING_B6);)
                // Now multiply template and micrograph to calculate the cross-correlation
                for (long int n = 0; n < Faux.size(); n++) {
                    Faux[n] = conj(Faux[n]) * Fmic[n];
                }

                // If we're not doing shrink, then Faux is bigger than Faux2!
                Faux2 = windowFourierTransform(Faux, workSize);
                CenterFFTbySign(Faux2);
                Maux = transformer.inverseFourierTransform(Faux2);
                #ifdef DEBUG
                tt() = Maux * normfft;
                tt.write("Mcc.spi");
                #endif

                // Calculate ratio of prabilities P(ref)/P(zero)
                // Keep track of the best values and their corresponding iref and psi

                // So now we already had precalculated: Mdiff2 = 1/sig*Sum(X^2) - 2/sig*Sum(X) + mu^2/sig*Sum(1)
                // Still to do (per reference): - 2/sig*Sum(AX) + 2*mu/sig*Sum(A) + Sum(A^2)
                for (long int n = 0; n < Maux.size(); n++) {
                    RFLOAT diff2 = -2.0 * normfft * Maux[n];
                    diff2 += 2.0 * Mmean[n] * sum_ref_under_circ_mask;
                    if (Mstddev[n] > 1E-10) { diff2 /= Mstddev[n]; }
                    diff2 += sum_ref2_under_circ_mask;
                    diff2 = exp(-diff2 / 2.0); // exponentiate to reflect the Gaussian error model. sigma=1 after normalization, 0.4=1/sqrt(2pi)

                    // Store fraction of (1 - probability-ratio) wrt  (1 - expected Pratio)
                    diff2 = (diff2 - 1.0) / (expected_Pratio - 1.0);
                    #ifdef DEBUG
                    Maux[n] = diff2;
                    #endif
                    if (diff2 > Mccf_best[n]) {
                        Mccf_best[n] = diff2;
                        Mpsi_best[n] = psi;
                    }
                }
                #ifdef DEBUG
                std::cerr << " Maux.max()= " << Maux.max() << std::endl;
                tt() = Maux;
                tt.write("Mccf.spi");
                std::cerr << " Press any key to continue... "  << std::endl;
                char c;
                std::cin >> c;
                #endif
                is_first_psi = false;
                }
            }
            }

            {
            ifdefTIMING(TicToc tt (timer, TIMING_B7);)
            if (do_write_fom_maps && !autopick_helical_segments) {

                Image<RFLOAT> I_bestCCF (Mccf_best);
                I_bestCCF.header.setValue(EMDL::IMAGE_STATS_MAX, expected_Pratio, I_bestCCF.header.size() - 1);  // Store expected_Pratio in the header of the image
                const auto fn_bestCCF = FileName::compose(getOutputRootName(fn_mic) + "_" + fn_out + "_ref", iref, "_bestCCF.spi");
                I_bestCCF.write(fn_bestCCF);

                Image<RFLOAT> I_bestPSI (Mpsi_best);
                const auto fn_bestPSI = FileName::compose(getOutputRootName(fn_mic) + "_" + fn_out + "_ref", iref, "_bestPSI.spi");
                I_bestPSI.write(fn_bestPSI);

                // for (long int n=0; n<((Mccf_best).size() / 10); n+=1) {
                //     std::cerr << Mccf_best[n] << std::endl;
                // }
                // exit(0);
            }
            }
        }

        {
        ifdefTIMING(TicToc tt (timer, TIMING_B8);)
        if (autopick_helical_segments) {
            if (!do_read_fom_maps) {
                // Combine Mccf_best and Mpsi_best from all refs
                for (long int n = 0; n < Mccf_best.size(); n++) {
                    RFLOAT new_ccf = Mccf_best[n];
                    RFLOAT old_ccf = Mccf_best_combined[n];
                    if (new_ccf > old_ccf) {
                        Mccf_best_combined[n] = new_ccf;
                        if (do_amyloid) {
                            Mpsi_best_combined[n] = Mpsi_best[n];
                        } else {
                            Mclass_best_combined[n] = iref;
                        }
                    }
                }
            }
        } else {
            // Now that we have Mccf_best and Mpsi_best, get the peaks
            std::vector<Peak> my_ref_peaks;

            Mstddev.setXmippOrigin();
            Mmean.setXmippOrigin();
            Mccf_best.setXmippOrigin();
            Mpsi_best.setXmippOrigin();

            peakSearch(Mccf_best, Mpsi_best, Mstddev, Mmean, iref, my_skip_side, my_ref_peaks, scale);
            prunePeakClusters(my_ref_peaks, min_distance_pix, scale);
            // Extend peaks by those of this reference
            peaks.insert(peaks.end(), my_ref_peaks.begin(), my_ref_peaks.end());
        }
    }
    }

    if (autopick_helical_segments) {
        RFLOAT thres = min_fraction_expected_Pratio;
        int peak_r_min = 1;
        std::vector<ccfPeak> ccf_peak_list;
        std::vector<std::vector<ccfPeak>> tube_coord_list, tube_track_list;
        std::vector<RFLOAT> tube_len_list;
        MultidimArray<RFLOAT> Mccfplot;

        if (do_write_fom_maps) {

            FileName fn_combinedCCF = getOutputRootName(fn_mic) + "_" + fn_out + "_combinedCCF.spi";
            Image<RFLOAT>(Mccf_best_combined).write(fn_combinedCCF);

            if (do_amyloid) {
                FileName fn_combinedPSI = getOutputRootName(fn_mic) + "_" + fn_out + "_combinedPSI.spi";
                Image<RFLOAT>(Mpsi_best_combined).write(fn_combinedPSI);
            } else {
                FileName fn_combinedCLASS = getOutputRootName(fn_mic) + + "_" + fn_out + "_combinedCLASS.spi";
                Image<int>(Mclass_best_combined).write(fn_combinedCLASS);
            }
        }

        Mccf_best_combined.setXmippOrigin();
        Mclass_best_combined.setXmippOrigin();
        Mpsi_best_combined.setXmippOrigin();
        Mstddev2.setXmippOrigin();
        Mavg.setXmippOrigin();
        if (do_amyloid) {
            pickAmyloids(
                Mccf_best_combined, Mpsi_best_combined, Mstddev2, Mavg,
                thres, amyloid_max_psidiff, fn_mic, fn_out,
                helical_tube_diameter / angpix, autopick_skip_side, scale
            );
        } else {
            pickCCFPeaks(
                Mccf_best_combined, Mstddev2, Mavg, Mclass_best_combined,
                thres, peak_r_min, particle_diameter / angpix,
                ccf_peak_list, Mccfplot, my_skip_side, scale
            );
            extractHelicalTubes(
                ccf_peak_list, tube_coord_list, tube_len_list, tube_track_list,
                particle_diameter / angpix, helical_tube_curvature_factor_max,
                min_particle_distance / angpix, helical_tube_diameter / angpix, scale
            );
            exportHelicalTubes(
                Mccf_best_combined, Mccfplot, Mclass_best_combined,
                tube_coord_list, tube_track_list, tube_len_list,
                fn_mic, fn_out,
                particle_diameter / angpix,
                helical_tube_length_min / angpix,
                my_skip_side, scale
            );
        }


        if ((do_write_fom_maps || do_read_fom_maps) && !do_amyloid) {

            Image<RFLOAT> It (Mccfplot);
            FileName fn_combinedPLOT = getOutputRootName(fn_mic) + "_" + fn_out + "_combinedPLOT.spi";
            It.write(fn_combinedPLOT);
        }
    } else {

        {
        ifdefTIMING(TicToc tt (timer, TIMING_B9);)
        //Now that we have done all references, prune the list again...
        prunePeakClusters(peaks, min_distance_pix, scale);
        // And remove all too close neighbours
        removeTooCloselyNeighbouringPeaks(peaks, min_distance_pix, scale);
        // Write out a STAR file with the coordinates
        MetaDataTable MDout;
        for (const Peak &peak : peaks) {
            const long int i = MDout.addObject();
            MDout.setValue(EMDL::IMAGE_COORD_X, (RFLOAT) peak.x / scale, i);
            MDout.setValue(EMDL::IMAGE_COORD_Y, (RFLOAT) peak.y / scale, i);
            MDout.setValue(EMDL::PARTICLE_CLASS,        peak.ref + 1, i); // start counting at 1
            MDout.setValue(EMDL::PARTICLE_AUTOPICK_FOM, peak.fom, i);
            MDout.setValue(EMDL::ORIENT_PSI,            peak.psi, i);
        }
        const FileName fn = getOutputRootName(fn_mic) + "_" + fn_out + ".star";
        MDout.write(fn);
        }

    }
}

FileName AutoPicker::getOutputRootName(FileName fn_mic) {
    FileName fn_pre, fn_jobnr, fn_post;
    decomposePipelineFileName(fn_mic, fn_pre, fn_jobnr, fn_post);
    return fn_odir + fn_post.withoutExtension();
}

void AutoPicker::calculateStddevAndMeanUnderMask(
    const MultidimArray<Complex> &_Fmic, const MultidimArray<Complex> &_Fmic2,
    MultidimArray<Complex> &_Fmsk, int nr_nonzero_pixels_mask, MultidimArray<RFLOAT> &_Mstddev, MultidimArray<RFLOAT> &_Mmean
) {
    FourierTransformer transformer;

    _Mstddev.initZeros(workSize, workSize);
    RFLOAT normfft = (RFLOAT) (micrograph_size * micrograph_size) / (RFLOAT) nr_nonzero_pixels_mask;

    // Calculate convolution of micrograph and mask, to get average under mask at all points
    MultidimArray<Complex> Faux;
    Faux.resize(_Fmic);
    #ifdef DEBUG
    Image<RFLOAT> tt;
    #endif

    for (long int n = 0; n < Faux.size(); n++) {
        Faux[n] = _Fmic[n] * conj(_Fmsk[n]);
    }
    MultidimArray<Complex> Faux2 = windowFourierTransform(Faux, workSize);
    CenterFFTbySign(Faux2);
    MultidimArray<RFLOAT> Maux = transformer.inverseFourierTransform(Faux2) * normfft;
    _Mmean = Maux;

    #ifdef DEBUG
    Image<RFLOAT>(Maux).write("Mavg_mic.spi");
    #endif

    // store minus average-squared already in _Mstddev
    _Mstddev = - Maux * Maux;

    // Calculate convolution of micrograph-squared and mask
    for (long int n = 0; n < Faux.size(); n++) {
        Faux[n] = _Fmic2[n] * conj(_Fmsk[n]);
    }
    Faux2 = windowFourierTransform(Faux, workSize);
    CenterFFTbySign(Faux2);
    Maux = transformer.inverseFourierTransform(Faux2);

    for (long int n = 0; n < _Mstddev.size(); n++) {
        // we already stored minus average-squared in _Mstddev
        _Mstddev[n] += normfft * Maux[n];
        _Mstddev[n] = _Mstddev[n] > (RFLOAT) 1E-10 ? sqrt(_Mstddev[n]) : 1.0;
    }

    #ifdef DEBUG
    Image<RFLOAT>(_Mstddev).write("Msig_mic.spi");
    #endif
}

void AutoPicker::peakSearch(
    const MultidimArray<RFLOAT> &Mfom,    const MultidimArray<RFLOAT> &Mpsi,
    const MultidimArray<RFLOAT> &Mstddev, const MultidimArray<RFLOAT> &Mmean,
    int iref, int skip_side, std::vector<Peak> &peaks, float scale
) {

    peaks.clear();
    Peak peak;
    peak.ref = iref;

    skip_side = (float) skip_side * scale;

    // Skip the pixels along the side of the micrograph!
    // At least 1, so dont have to check for the borders!
    skip_side = std::max(1, skip_side);
    for (
        int j  = Xmipp::init((int) ((float) micrograph_ysize * scale)) + skip_side;
            j <= Xmipp::last((int) ((float) micrograph_ysize * scale)) - skip_side;
            j++
    ) {
    for (
        int i  = Xmipp::init((int) ((float) micrograph_xsize * scale)) + skip_side;
            i <= Xmipp::last((int) ((float) micrograph_xsize * scale)) - skip_side;
            i++
    ) {

            RFLOAT x = Mfom.elem(i, j);
            if (x < min_fraction_expected_Pratio) continue;
            // Otherwise, if this element is above the threshold:

            // Only check stddev in the noise areas if max_stddev_noise is positive!
            if (max_stddev_noise > 0.0 && Mstddev.elem(i, j) > max_stddev_noise)
                continue;
            if (min_avg_noise > -900.0 && Mmean.elem(i, j) < min_avg_noise)
                continue;

            if (scale < 1.0) {
                // When we use shrink, then often peaks aren't 5 pixels big anymore...
                if (Mfom.elem(i - 1, j) > x) continue;
                if (Mfom.elem(i + 1, j) > x) continue;
                if (Mfom.elem(i, j - 1) > x) continue;
                if (Mfom.elem(i, j + 1) > x) continue;
            } else {
                // This is a peak if all four neighbours are also above the threshold, AND have lower values than x
                if (Mfom.elem(i - 1, j) < min_fraction_expected_Pratio || Mfom.elem(i - 1, j) > x) continue;
                if (Mfom.elem(i + 1, j) < min_fraction_expected_Pratio || Mfom.elem(i + 1, j) > x) continue;
                if (Mfom.elem(i, j - 1) < min_fraction_expected_Pratio || Mfom.elem(i, j - 1) > x) continue;
                if (Mfom.elem(i, j + 1) < min_fraction_expected_Pratio || Mfom.elem(i, j + 1) > x) continue;
            }
            peak.x = i - Xmipp::init((float) micrograph_xsize * scale);
            peak.y = j - Xmipp::init((float) micrograph_ysize * scale);
            peak.psi = Mpsi.elem(i, j);
            peak.fom = Mfom.elem(i, j);
            peak.relative_fom = x;
            peaks.push_back(peak);
        }
    }
}

void AutoPicker::prunePeakClusters(
    std::vector<Peak> &peaks, int min_distance, float scale
) {
    int nclus = 0;

    std::vector<Peak> pruned_peaks;
    while (!peaks.empty()) {
        nclus++;

        // Make a cluster, containing the first peak in peaks 
        // and all other peaks within particle_radius of it.
        std::vector<Peak> cluster;
        cluster.push_back(peaks[0]);
        peaks.erase(peaks.begin());
        for (int i = 0; i < cluster.size(); ++i) {  // Loop by index (iterators will be invalidated)
            const Peak &peak1 = cluster[i];
            for (auto peak2_itr = peaks.begin(); peak2_itr != peaks.end(); ++peak2_itr) {
                if ((float) dist2(peak1, *peak2_itr) < (float) particle_radius2 * scale * scale) {
                    // Put *peak2_itr in the cluster, and remove from the peaks list
                    cluster.push_back(*peak2_itr);
                    peaks.erase(peak2_itr);
                }
            }
        }

        float mind2 = (float) (min_distance * min_distance) * scale * scale;
        // Now search for the peak from the cluster with the best ccf.
        // Then search again if there are any other peaks in the cluster that are further than particle_diameter apart from the selected peak
        // If so, again search for the maximum
        while (!cluster.empty()) {

            // Peak with greatest relative FoM
            Peak bestpeak = *std::max_element(
                cluster.begin(), cluster.end(),
                [] (const Peak &lhs, const Peak &rhs) {
                    return lhs.relative_fom < rhs.relative_fom;
                }
            );

            // Store this peak as pruned
            pruned_peaks.push_back(bestpeak);

            // Remove from the cluster all peaks within mind2 of the best peak
            cluster.erase(std::remove_if(
                cluster.begin(), cluster.end(),
                [&bestpeak, &mind2] (const Peak &peak) {
                    return (float) dist2(peak, bestpeak) < mind2;
                }
            ), cluster.end());
        }
    }

    // Set the pruned peaks back into the input vector
    peaks = pruned_peaks;

}

void AutoPicker::removeTooCloselyNeighbouringPeaks(
    std::vector<Peak> &peaks, int min_distance, float scale
) {
    // Remove peaks that are within min_distance of some other peak
    float least_dist2_to_other_cutoff = (float) (min_distance * min_distance) * scale * scale;
    peaks.erase(std::remove_if(peaks.begin(), peaks.end(),
        [&peaks, &least_dist2_to_other_cutoff] (const Peak &peak1) -> bool {
            // The squared least distance between this peak and any other
            float least_dist2_to_other = 9999999999.0;
            for (auto &peak2 : peaks) {
                if (&peak1 != &peak2) {
                    float d2 = dist2(peak1, peak2);
                    if (d2 < least_dist2_to_other)
                        least_dist2_to_other = d2;
                }
            }
            return least_dist2_to_other <= least_dist2_to_other_cutoff;
        }
    ), peaks.end());
}

int AutoPicker::largestPrime(int query) {
    int i(2), primeF(query);
    while (i * i <= primeF) {
        if (primeF % i != 0) {
            i += 1;
        } else {
            primeF /= i;
        }
    }
    return primeF;
}

int AutoPicker::getGoodFourierDims(int requestedSizeRealX, int lim) {

    if (!do_optimise_scale)
        return requestedSizeRealX;

    int inputPrimeF = std::max(largestPrime(requestedSizeRealX), largestPrime(requestedSizeRealX / 2 + 1));
    if (inputPrimeF <= LARGEST_ACCEPTABLE_PRIME) {
        if (verb > 0)
            std::cout << " + Will use micrographs scaled to " << requestedSizeRealX << " pixels as requested. The largest prime factor in FFTs is " << inputPrimeF << std::endl;
        return requestedSizeRealX;
    }

    int S_up = LARGEST_ACCEPTABLE_PRIME;
    int S_down = LARGEST_ACCEPTABLE_PRIME;

    // Search upwards - can take a long time if unlucky and/or small LARGEST_ACCEPTABLE_PRIME
    int currentU = requestedSizeRealX;
    S_up = 			largestPrime(currentU);
    S_up = std::max(largestPrime(currentU / 2 + 1),S_up);
    while (S_up >= LARGEST_ACCEPTABLE_PRIME && currentU <= lim + 2) {
        currentU += 2;
        S_up =          largestPrime(currentU);
        S_up = std::max(largestPrime(currentU / 2 + 1), S_up);
    }

    // Search downwards - guaranteed to find in reasonable time
    int currentD = requestedSizeRealX;
    S_down =          largestPrime(currentD);
    S_down = std::max(largestPrime(currentD / 2 + 1), S_down);
    while (S_down >= LARGEST_ACCEPTABLE_PRIME) {
        currentD -= 2;
        S_down =          largestPrime(currentD);
        S_down = std::max(largestPrime(currentD / 2 + 1), S_down);
    }

    if (verb > 0) {
        std::cout << " + WARNING: Requested rescale of micrographs is " << requestedSizeRealX << " pixels. The largest prime factor in FFTs is " << inputPrimeF << std::endl;
    }
    if (currentU - requestedSizeRealX > requestedSizeRealX - currentD || currentU > lim) {
        if (verb > 0) {
            std::cout << " + WARNING: Will change rescaling of micrographs to " << currentD << " pixels, because the prime factor then becomes " <<  S_down << std::endl;
            std::cout << " + WARNING: add --skip_optimise_scale to your autopick command to prevent rescaling " << std::endl;
        }
        return currentD;
    } else {
        if (verb > 0) {
            std::cout << " + WARNING: Will change rescaling of micrographs to " << currentU << " pixels, because the prime factor then becomes " <<  S_up << std::endl;
            std::cout << " + WARNING: add --skip_optimise_scale to your autopick command to prevent rescaling " << std::endl;
        }
        return currentU;
    }
}
