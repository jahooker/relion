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

#include <src/image.h>
#include "src/colour.h"
#include <src/funcs.h>
#include <src/args.h>
#include <src/fftw.h>
#include <src/time.h>
#include <src/symmetries.h>
#include <src/jaz/obs_model.h>
#include "src/jaz/img_proc/image_op.h"
#ifdef HAVE_PNG
#include <src/jaz/gravis/tImage.h>
#endif

#include <map>

// Will only issue warning once, even when called multiple times
void issue_division_warning() {
    static bool called = false;
    if (!called) {
        std::cout << "Warning: ignore very small pixel values in divide image..." << std::endl;
        called = true;
    }
}

RFLOAT get_scale(
    const MultidimArray<RFLOAT> &W,
    const MultidimArray<RFLOAT> &X,
    const MultidimArray<RFLOAT> &A
) {
    RFLOAT sum_aa = 0.0, sum_xa = 0.0;
    // sum_aa = sum(W * W * X * X)
    // sum_xa = sum(W * W * A * X)
    for (long int n = 0; n < X.size(); n++) {
        RFLOAT w = W[n];
        RFLOAT x = X[n];
        RFLOAT a = A[n];
        sum_aa += w * w * a * a;
        sum_xa += w * w * x * a;
    }
    return sum_xa / sum_aa;
}


template <typename T, typename F>
inline T maybe(T t, F f) {
    try { return f(); } catch (const char *errmsg) { return t; }
}

void rescale(
    Image<RFLOAT> &img,
    RFLOAT angpix, RFLOAT requested_angpix, RFLOAT real_angpix,
    int &my_new_box_size
) {
    int oldxsize = Xsize(img());
    int oldysize = Ysize(img());
    int oldsize = oldxsize;
    if (oldxsize != oldysize && img().getDim() == 2) {
        oldsize = std::max(oldxsize, oldysize);
        img() = img().setXmippOrigin().windowed(
            Xmipp::init(oldsize), Xmipp::last(oldsize),
            Xmipp::init(oldsize), Xmipp::last(oldsize)
        );
    }

    int newsize = make_even(round(oldsize * (angpix / requested_angpix)));

    real_angpix = oldsize * angpix / newsize;
    if (fabs(real_angpix - requested_angpix) / requested_angpix > 0.001)
        std::cerr << "WARNING: The requested pixel size (--rescale_angpix) is " << requested_angpix << " A/px. "
        "However, because the box size will be trimmed to an even number "
        "(" << newsize << " in this case), "
        "the actual pixel size will be " << real_angpix << " A/px. "
        "The actual pixel size will be written into the image header, "
        "unless you use --force_header_angpix." << std::endl;

    resizeMap(img(), newsize);
    my_new_box_size = newsize;

    if (oldxsize != oldysize && img().getDim() == 2) {
        const int new_xdim = make_even(round(oldxsize * (angpix / real_angpix)));
        const int new_ydim = make_even(round(oldysize * (angpix / real_angpix)));
        img() = img().setXmippOrigin().windowed(
            Xmipp::init(new_xdim), Xmipp::last(new_xdim), 
            Xmipp::init(new_ydim), Xmipp::last(new_ydim)
        );
    }

    // Also reset the sampling rate in the header
    img.setSamplingRateInHeader(real_angpix);
}

class image_handler_parameters {

    public:

    FileName fn_in, fn_out, fn_sel, fn_img, fn_sym, fn_sub, fn_mult, fn_div, fn_add, fn_subtract, fn_mask, fn_fsc, fn_adjust_power, fn_correct_ampl, fn_fourfilter, fn_cosDPhi;

    int bin_avg, avg_first, avg_last, edge_x0, edge_xF, edge_y0, edge_yF, filter_edge_width, new_box, minr_ampl_corr, my_new_box_size;

    bool do_add_edge, do_invert_hand, do_flipXY, do_flipmXY, do_flipZ, do_flipX, do_flipY, do_shiftCOM, do_stats, do_calc_com, do_avg_ampl, do_avg_ampl2, do_avg_ampl2_ali, do_average, do_remove_nan, do_average_all_frames, do_power, do_ignore_optics, do_optimise_scale_subtract;

    RFLOAT multiply_constant, divide_constant, add_constant, subtract_constant, threshold_above, threshold_below, angpix, requested_angpix, real_angpix, force_header_angpix, lowpass, highpass, logfilter, bfactor, shift_x, shift_y, shift_z, replace_nan, randomize_at, optimise_bfactor_subtract;
    // PNG options
    RFLOAT minval, maxval, sigma_contrast;
    ColourScheme color_scheme;  // There is a global variable called colour_scheme in displayer.h!

    std::string directional;
    int verb;
    // I/O Parser
    IOParser parser;
    ObservationModel obsModel;

    Image<RFLOAT> Iout;
    Image<RFLOAT> Iop;
    Image<RFLOAT> Imask;
    MultidimArray<RFLOAT> avg_ampl;
    MetaDataTable MD;
    FourierTransformer transformer;
    std::map<FileName, long int> n_images;

    // Image size
    int xdim, ydim, zdim;
    long int ndim;

    void usage() {
        parser.writeUsage(std::cerr);
    }

    void read(int argc, char **argv) {

        parser.setCommandLine(argc, argv);

        const int general_section = parser.addSection("General options");
        fn_in  = parser.getOption("--i", "Input STAR file, image (.mrc) or movie/stack (.mrcs)");
        fn_out = parser.getOption("--o", "Output name (for STAR-input: insert this string before each image's extension)", "");

        const int cst_section = parser.addSection("image-by-constant operations");
        multiply_constant = textToFloat(parser.getOption("--multiply_constant", "Multiply the image(s) pixel values by this constant",   "1.0"));
        divide_constant   = textToFloat(parser.getOption("--divide_constant",   "Divide the image(s) pixel values by this constant",     "1.0"));
        add_constant      = textToFloat(parser.getOption("--add_constant",      "Add this constant to the image(s) pixel values",        "0.0"));
        subtract_constant = textToFloat(parser.getOption("--subtract_constant", "Subtract this constant from the image(s) pixel values", "0.0"));
        threshold_above   = textToFloat(parser.getOption("--threshold_above", "Set all values higher than this value to this value", "+999.0"));
        threshold_below   = textToFloat(parser.getOption("--threshold_below", "Set all values lower than this value to this value",  "-999.0"));

        const int img_section = parser.addSection("image-by-image operations");
        fn_mult         = parser.getOption("--multiply", "Multiply input image(s) by the pixel values in this image",     "");
        fn_div          = parser.getOption("--divide",   "Divide input image(s) by the pixel values in this image",       "");
        fn_add          = parser.getOption("--add",      "Add the pixel values in this image to the input image(s)",      "");
        fn_subtract     = parser.getOption("--subtract", "Subtract the pixel values in this image to the input image(s)", "");
        fn_fsc          = parser.getOption("--fsc",      "Calculate FSC curve of the input image with this image", "");
        do_power        = parser.checkOption("--power",  "Calculate power spectrum (|F|^2) of the input image");
        fn_adjust_power = parser.getOption("--adjust_power",   "Adjust the power spectrum of the input image to be the same as this image", "");
        fn_fourfilter   = parser.getOption("--fourier_filter", "Multiply the Fourier transform of the input image(s) with this one image",  "");

        const int subtract_section = parser.addSection("additional subtract options");
        do_optimise_scale_subtract = parser.checkOption("--optimise_scale_subtract", "Optimise scale between maps before subtraction?");
        optimise_bfactor_subtract = textToFloat(parser.getOption("--optimise_bfactor_subtract", "Search range for relative B-factor for subtraction (in A^2)", "0.0"));
        fn_mask = parser.getOption("--mask_optimise_subtract", "Use only voxels in this mask to optimise scale for subtraction", "");

        const int four_section = parser.addSection("per-image operations");
        do_stats    = parser.checkOption("--stats", "Calculate per-image statistics?");
        do_calc_com = parser.checkOption("--com", "Calculate center of mass?");
        bfactor  = textToFloat(parser.getOption("--bfactor", "Apply a B-factor (in A^2)", "0.0"));
        lowpass  = textToFloat(parser.getOption("--lowpass", "Low-pass filter frequency (in A)", "-1.0"));
        highpass = textToFloat(parser.getOption("--highpass", "High-pass filter frequency (in A)", "-1.0"));
        directional = parser.getOption("--directional", "Directionality of low-pass filter frequency ('X', 'Y' or 'Z', default non-directional)", "");
        logfilter        = textToFloat(parser.getOption("--LoG", "Diameter for optimal response of Laplacian of Gaussian filter (in A)", "-1.0"));
        angpix           = textToFloat(parser.getOption("--angpix", "Pixel size (in A)", "-1"));
        requested_angpix = textToFloat(parser.getOption("--rescale_angpix", "Scale input image(s) to this new pixel size (in A)", "-1.0"));
        real_angpix = -1;
        force_header_angpix = textToFloat(parser.getOption("--force_header_angpix", "Change the pixel size in the header (in A). "
            "Without --rescale_angpix, the image is not scaled.", "-1.0"));
        new_box           = textToInteger(parser.getOption("--new_box", "Resize the image(s) to this new box size (in pixel) ", "-1"));
        filter_edge_width = textToInteger(parser.getOption("--filter_edge_width", "Width of the raised cosine on the low/high-pass filter edge (in resolution shells)", "2"));
        do_flipX = parser.checkOption("--flipX", "Flip (mirror) a 2D image or 3D map in the X-direction?");
        do_flipY = parser.checkOption("--flipY", "Flip (mirror) a 2D image or 3D map in the Y-direction?");
        do_flipZ = parser.checkOption("--flipZ", "Flip (mirror) a 3D map in the Z-direction?");
        do_invert_hand = parser.checkOption("--invert_hand", "Invert hand by flipping X? Similar to flipX, but preserves the symmetry origin. "
            "Edge pixels are wrapped around.");
        do_shiftCOM    = parser.checkOption("--shift_com", "Shift image(s) to their center-of-mass (only on positive pixel values)");
        shift_x = textToFloat(parser.getOption("--shift_x", "Shift images this many pixels in the X-direction", "0"));
        shift_y = textToFloat(parser.getOption("--shift_y", "Shift images this many pixels in the Y-direction", "0"));
        shift_z = textToFloat(parser.getOption("--shift_z", "Shift images this many pixels in the Z-direction", "0"));
        do_avg_ampl      = parser.checkOption("--avg_ampl",      "Calculate average amplitude spectrum for all images?");
        do_avg_ampl2     = parser.checkOption("--avg_ampl2",     "Calculate average amplitude spectrum for all images?");
        do_avg_ampl2_ali = parser.checkOption("--avg_ampl2_ali", "Calculate average amplitude spectrum for all aligned images?");
        do_average       = parser.checkOption("--average", "Calculate average of all images (without alignment)");
        fn_correct_ampl  = parser.getOption("--correct_avg_ampl", "Correct all images with this average amplitude spectrum", "");
        minr_ampl_corr   = textToInteger(parser.getOption("--minr_ampl_corr", "Minimum radius (in Fourier pixels) to apply average amplitudes", "0"));
        do_remove_nan    = parser.checkOption("--remove_nan", "Replace non-numerical values (NaN, inf, etc) in the image(s)");
        replace_nan      = textToFloat(parser.getOption("--replace_nan", "Replace non-numerical values (NaN, inf, etc) with this value", "0"));
        randomize_at     = textToFloat(parser.getOption("--phase_randomise", "Randomise phases beyond this resolution (in Angstroms)", "-1"));

        const int three_d_section = parser.addSection("3D operations");
        fn_sym = parser.getOption("--sym", "Symmetrise 3D map with this point group (e.g. D6)", "");

        const int preprocess_section = parser.addSection("2D-micrograph (or movie) operations");
        do_flipXY   = parser.checkOption("--flipXY",   "Flip the image(s) in the XY direction?");
        do_flipmXY  = parser.checkOption("--flipmXY",  "Flip the image(s) in the -XY direction?");
        do_add_edge = parser.checkOption("--add_edge", "Add a barcode-like edge to the micrograph/movie frames?");
        edge_x0 = textToInteger(parser.getOption("--edge_x0", "Pixel column to be used for the left edge",  "0"));
        edge_y0 = textToInteger(parser.getOption("--edge_y0", "Pixel row to be used for the top edge",      "0"));
        edge_xF = textToInteger(parser.getOption("--edge_xF", "Pixel column to be used for the right edge", "4095"));
        edge_yF = textToInteger(parser.getOption("--edge_yF", "Pixel row to be used for the bottom edge",   "4095"));

        const int avg_section = parser.addSection("Movie-frame averaging options");
        bin_avg   = textToInteger(parser.getOption("--avg_bin",   "Width (in frames) for binning average, i.e. of every so-many frames", "-1"));
        avg_first = textToInteger(parser.getOption("--avg_first", "First frame to include in averaging",                                 "-1"));
        avg_last  = textToInteger(parser.getOption("--avg_last",  "Last frame to include in averaging",                                  "-1"));
        do_average_all_frames = parser.checkOption("--average_all_movie_frames", "Average all movie frames of all movies in the input STAR file.");

        const int png_section = parser.addSection("PNG options");
        minval = textToFloat(parser.getOption("--black", "Pixel value for black (default is auto-contrast)", "0"));
        maxval = textToFloat(parser.getOption("--white", "Pixel value for white (default is auto-contrast)", "0"));
        sigma_contrast = textToFloat(parser.getOption("--sigma_contrast", "Set white and black pixel values this many times the image stddev from the mean", "0"));

        color_scheme = parser.getColourScheme();

        // Hidden
        fn_cosDPhi = getParameter(argc, argv, "--cos_dphi", "");
        // Check for errors in the command-line option
        if (parser.checkForErrors())
            REPORT_ERROR("Errors encountered on the command line (see above), exiting...");

        verb = !do_stats && !do_calc_com && fn_fsc.empty() && fn_cosDPhi.empty() && !do_power;

        if (fn_out.empty() && verb == 1)
            REPORT_ERROR("Please specify the output file name with --o.");
    }

    void perImageOperations(Image<RFLOAT> &Iin, FileName &my_fn_out, RFLOAT psi = 0.0) {
        Image<RFLOAT> Iout;
        Iout().resize(Iin());

        bool isPNG = FileName(my_fn_out.getExtension()).toLowercase() == "png";
        if (isPNG && (Zsize(Iout()) > 1 || Nsize(Iout()) > 1))
            REPORT_ERROR("You can only write a 2D image to a PNG file.");

        if (angpix < 0 && (
            requested_angpix > 0 || randomize_at > 0 || do_power ||
            !fn_fsc.empty() || !fn_cosDPhi.empty() || !fn_correct_ampl.empty() ||
            logfilter > 0 || lowpass > 0 || highpass > 0 ||
            fabs(bfactor) > 0 || fabs(optimise_bfactor_subtract) > 0
        )) {
            angpix = Iin.samplingRateX();
            std::cerr << "WARNING: You did not specify --angpix. The pixel size in the image header, " << angpix << " A/px, is used." << std::endl;
        }

        if (do_add_edge) {
            // Treat boundaries
            for (long int j = 0; j < Ysize(Iin()); j++)
            for (long int i = 0; i < Xsize(Iin()); i++) {
                if (i < edge_x0) {
                    direct::elem(Iin(), i, j) = direct::elem(Iin(), edge_x0, j);
                } else if (i > edge_xF) {
                    direct::elem(Iin(), i, j) = direct::elem(Iin(), edge_xF, j);
                }
                if (j < edge_y0) {
                    direct::elem(Iin(), i, j) = direct::elem(Iin(), i, edge_y0);
                } else if (j > edge_yF) {
                    direct::elem(Iin(), i, j) = direct::elem(Iin(), i, edge_yF);
                }
            }
        }

        // Flipping: this needs to be done from Iin to Iout (i.e. can't be done in-place on Iout)
        if (do_flipXY) {
            // Flip X/Y
            for (long int j = 0; j < Ysize(Iin()); j++)
            for (long int i = 0; i < Xsize(Iin()); i++) {
                direct::elem(Iout(), i, j) = direct::elem(Iin(), j, i);
                // if (i < j)
                // std::swap(direct::elem(Iin(), i, j), direct::elem(Iin(), j, i));
            }
        } else if (do_flipmXY) {
            // Flip mX/Y
            for (long int j = 0; j < Ysize(Iin()); j++)
            for (long int i = 0; i < Xsize(Iin()); i++) {
                direct::elem(Iout(), i, j) = direct::elem(Iin(), Xsize(Iin()) - 1 - j, Ysize(Iin()) - 1 - i);
            }
        } else {
            Iout = Iin;
        }

        // From here on also 3D options
        if (do_remove_nan) {
            Iout().setXmippOrigin();
            for (long int k = 0; k < Zsize(Iout()); k++)
            for (long int j = 0; j < Ysize(Iout()); j++)
            for (long int i = 0; i < Xsize(Iout()); i++) {
                if (std::isnan(direct::elem(Iout(), i, j, k)) || std::isinf(direct::elem(Iout(), i, j, k)))
                    direct::elem(Iout(), i, j, k) = replace_nan;
            }
        }

        if (randomize_at > 0.0) {
            const int iran = Xsize(Iin()) * angpix / randomize_at;
            Iout = Iin;
            Iout() = randomizePhasesBeyond(Iout(), iran);
        }

        if (fabs(multiply_constant - 1.0) > 0.0) {
            Iin() *= multiply_constant;
        } else if (fabs(divide_constant - 1.0) > 0.0) {
            Iin() /= divide_constant;
        } else if (fabs(add_constant) > 0.0) {
            Iin() += add_constant;
        } else if (fabs(subtract_constant) > 0.0) {
            Iin() -= subtract_constant;
        } else if (!fn_mult.empty()) {
            Iout() *= Iop();
        } else if (!fn_div.empty()) {
            for (long int k = 0; k < Zsize(Iin()); k++)
            for (long int j = 0; j < Ysize(Iin()); j++)
            for (long int i = 0; i < Xsize(Iin()); i++) {
                if (abs(direct::elem(Iop(), i, j, k)) < 1e-10) {
                    issue_division_warning();
                    direct::elem(Iout(), i, j, k) = 0.0;
                } else {
                    direct::elem(Iout(), i, j, k) /= direct::elem(Iop(), i, j, k);
                }
            }
        } else if (!fn_add.empty()) {
            Iout() += Iop();
        } else if (!fn_subtract.empty()) {
            RFLOAT scale = 1.0, best_diff2 ;
            if (do_optimise_scale_subtract) {
                if (fn_mask.empty()) {
                    Imask().resize(Iop());
                    Imask() = 1.0;
                }

                if (optimise_bfactor_subtract > 0.0) {
                    MultidimArray<RFLOAT> Isharp(Iop());
                    FourierTransformer transformer;
                    MultidimArray<Complex> FTop = transformer.FourierTransform(Iop());

                    RFLOAT bfac, smallest_diff2 = 99.0e99;
                    for (RFLOAT bfac_this_iter = -optimise_bfactor_subtract; bfac_this_iter <= optimise_bfactor_subtract; bfac_this_iter += 10.0) {
                        MultidimArray<Complex> FTop_bfac = FTop;
                        applyBFactorToMap(FTop_bfac, Xsize(Iop()), bfac_this_iter, angpix);
                        Isharp = transformer.inverseFourierTransform(FTop_bfac);

                        RFLOAT scale_this_iter = get_scale(Imask(), Iin(), Isharp);
                        RFLOAT diff2 = 0.0;
                        // diff2 = Imask * Imask * (Iin - scale_this_iter * Isharp) * (Iin - scale_this_iter * Isharp)
                        for (long int n = 0; n < Iin().size(); n++) {
                            RFLOAT w = Imask()[n];
                            RFLOAT x = Iin()[n];
                            RFLOAT a = Isharp[n];
                            RFLOAT b = x - scale_this_iter * a;
                            diff2 += w * w * b * b;
                        }
                        if (diff2 < smallest_diff2) {
                            smallest_diff2 = diff2;
                            bfac  = bfac_this_iter;
                            scale = scale_this_iter;
                        }
                    }
                    std::cout << " Optimised bfactor = " << bfac << "; optimised scale = " << scale << std::endl;
                    applyBFactorToMap(FTop, Xsize(Iop()), bfac, angpix);
                    Iop() = transformer.inverseFourierTransform(FTop);

                } else {
                    scale = get_scale(Imask(), Iin(), Iop());
                    std::cout << " Optimised scale = " << scale << std::endl;
                }
            }

            for (long int k = 0; k < Zsize(Iin()); k++)
            for (long int j = 0; j < Ysize(Iin()); j++)
            for (long int i = 0; i < Xsize(Iin()); i++) {
                direct::elem(Iout(), i, j, k) -= scale * direct::elem(Iop(), i, j, k);
                // Looks like a job for expression templates
            }
        } else if (!fn_fsc.empty()) {
            const auto fsc = getFSC(Iout(), Iop());
            MetaDataTable MDfsc;
            MDfsc.name = "fsc";
            for (long int i = 0; i < Xsize(fsc); i++) {
                MDfsc.addObject();
                RFLOAT res = i > 0 ? Xsize(Iout()) * angpix / (RFLOAT) i : 999.0;
                MDfsc.setValue(EMDL::SPECTRAL_IDX, (int) i, i);
                MDfsc.setValue(EMDL::RESOLUTION, 1.0 / res, i);     // Pixels per Angstrom
                MDfsc.setValue(EMDL::RESOLUTION_ANGSTROM, res, i);  // Angstroms per pixel
                MDfsc.setValue(EMDL::POSTPROCESS_FSC_GENERAL, direct::elem(fsc, i), i);
            }
            MDfsc.write(std::cout);
        } else if (do_power) {
            const auto spectrum = getSpectrum(Iout(), power);
            MetaDataTable MDpower;
            MDpower.name = "power";
            const long int Nyquist = Xsize(Iout()) / 2 + 1;
            for (long int i = 0; i <= Nyquist; i++) {
                MDpower.addObject();
                const RFLOAT res = i > 0 ? Xsize(Iout()) * angpix / (RFLOAT) i : 999.0;
                MDpower.setValue(EMDL::SPECTRAL_IDX, i, i);
                MDpower.setValue(EMDL::RESOLUTION, 1.0 / res, i);
                MDpower.setValue(EMDL::RESOLUTION_ANGSTROM, res, i);
                MDpower.setValue(EMDL::MLMODEL_POWER_REF, direct::elem(spectrum, i), i);
            }
            MDpower.write(std::cout);
        } else if (!fn_adjust_power.empty()) {
            const auto spectrum = getSpectrum(Iop(), amplitude);
            Iout() = adaptSpectrum(Iin(), spectrum, amplitude);
        } else if (!fn_cosDPhi.empty()) {

            FourierTransformer transformer;
            const MultidimArray<Complex> FT1 = transformer.FourierTransform(Iout());
            const MultidimArray<Complex> FT2 = transformer.FourierTransform(Iop());

            const std::vector<RFLOAT> cosDPhi = cosDeltaPhase(FT1, FT2);
            MetaDataTable MDcos;
            MDcos.name = "cos";
            for (long int i = 0; i < cosDPhi.size(); i++) {
                MDcos.addObject();
                const RFLOAT res = i > 0 ? Xsize(Iout()) * angpix / (RFLOAT) i : 999.0;
                MDcos.setValue(EMDL::SPECTRAL_IDX, (int) i, i);
                MDcos.setValue(EMDL::RESOLUTION, 1.0 / res, i);
                MDcos.setValue(EMDL::RESOLUTION_ANGSTROM, res, i);
                MDcos.setValue(EMDL::POSTPROCESS_FSC_GENERAL, cosDPhi[i], i);
            }
            MDcos.write(std::cout);
        } else if (!fn_correct_ampl.empty()) {
            MultidimArray<Complex> &FT = transformer.FourierTransform(Iin());
            FT /= avg_ampl;
            transformer.inverseFourierTransform();
            Iout = Iin;
        } else if (!fn_fourfilter.empty()) {
            MultidimArray<Complex> &FT = transformer.FourierTransform(Iin());

            // Note: only 2D rotations are done! 3D application assumes zero rot and tilt!
            Matrix<RFLOAT> A = rotation2DMatrix(psi);

            Iop().setXmippOrigin();
            FOR_ALL_ELEMENTS_IN_FFTW_TRANSFORM(FT) {
                int ipp = round(jp * A(0, 0) + ip * A(0, 1));
                int jpp = round(jp * A(1, 0) + ip * A(1, 1));
                int kpp = kp;
                RFLOAT fil = (
                    jpp >= Xinit(Iop()) && jpp <= Xlast(Iop()) &&
                    ipp >= Yinit(Iop()) && ipp <= Ylast(Iop())
                ) ? Iop().elem(ipp, jpp, kpp) : 0.0;
                direct::elem(FT, i, j, k) *= fil;
            }
            transformer.inverseFourierTransform();
            Iout = Iin;
        }

        if (fabs(bfactor) > 0.0)
            applyBFactorToMap(Iout(), bfactor, angpix);

        if (logfilter > 0.0) {
            LoGFilterMap(Iout(), logfilter, angpix);
            // Iout().statisticsAdjust(0, 1);
        }

        if (lowpass > 0.0) {
            if (directional.empty()) {
                lowPassFilterMap(Iout(), lowpass, angpix, filter_edge_width);
            } else {
                const int axis = directional.length() == 1 ?
                    std::string("xyz").find(tolower(directional[0])) : -1;
                directionalFilterMap(Iout(), lowpass, angpix, axis, filter_edge_width);
            }
        }

        if (highpass > 0.0)
            highPassFilterMap(Iout(), highpass, angpix, filter_edge_width);

        if (do_flipX || do_invert_hand) {
            ImageOp::flipX(Iin(), Iout());
        } else if (do_flipY) {
            ImageOp::flipY(Iin(), Iout());
        } else if (do_flipZ) {
            if (Zsize(Iout()) <= 1)
                REPORT_ERROR("ERROR: this map is not 3D, so flipping in Z makes little sense.");
            ImageOp::flipZ(Iin(), Iout());
        }

        // Shifting
        if (do_shiftCOM) {
            Iout() = translateCenterOfMassToCenter(Iout(), DONT_WRAP, true); // verbose
        } else if (
            fabs(shift_x) > 0.0 ||
            fabs(shift_y) > 0.0 ||
            fabs(shift_z) > 0.0
        ) {
            Vector<RFLOAT> shift (2 + (zdim > 1));
            XX(shift) = shift_x;
            YY(shift) = shift_y;
            if (zdim > 1)
            ZZ(shift) = shift_z;
            Iout() = translate(Iout(), shift, DONT_WRAP);
        }

        // Re-scale
        if (requested_angpix > 0.0)
            rescale(Iout, angpix, requested_angpix, real_angpix, my_new_box_size);

        // Re-window
        if (new_box > 0 && new_box != Xsize(Iout())) {
            Iout().setXmippOrigin();
            switch (Iout().getDim()) {
                case 2:
                Iout() = Iout().windowed(
                    Xmipp::init(new_box), Xmipp::last(new_box),
                    Xmipp::init(new_box), Xmipp::last(new_box)
                ); break;
                case 3:
                Iout() = Iout().windowed(
                    Xmipp::init(new_box), Xmipp::last(new_box),
                    Xmipp::init(new_box), Xmipp::last(new_box),
                    Xmipp::init(new_box), Xmipp::last(new_box)
                ); break;
            }
            my_new_box_size = new_box;
        }

        if (!fn_sym.empty())
            symmetriseMap(Iout(), fn_sym);

        // Thresholding (can be done after any other operation)
        if (fabs(threshold_above - 999.0) > 0.0) {
            for (long int k = 0; k < Zsize(Iout()); k++)
            for (long int j = 0; j < Ysize(Iout()); j++)
            for (long int i = 0; i < Xsize(Iout()); i++) {
                if (direct::elem(Iout(), i, j, k) > threshold_above)
                    direct::elem(Iout(), i, j, k) = threshold_above;
            }
        }
        if (fabs(threshold_below + 999.0) > 0.0) {
            for (long int k = 0; k < Zsize(Iout()); k++)
            for (long int j = 0; j < Ysize(Iout()); j++)
            for (long int i = 0; i < Xsize(Iout()); i++) {
                if (direct::elem(Iout(), i, j, k) < threshold_below)
                    direct::elem(Iout(), i, j, k) = threshold_below;
            }
        }

        if (force_header_angpix > 0) {
            Iout.setSamplingRateInHeader(force_header_angpix);
            std::cout << "As requested by --force_header_angpix, "
                << "the pixel size in the image header is set to " << force_header_angpix << " A/px." << std::endl;
        }

        // Write out the result
        // Check whether fn_out has an "@": if so REPLACE the corresponding frame in the output stack!
        long int n;
        FileName fn_tmp;
        my_fn_out.decompose(n, fn_tmp);
        n--;
        if (isPNG) {
            #ifdef HAVE_PNG
            const auto minmax = getImageContrast(Iout(), minval, maxval, sigma_contrast); // Update if necessary
            const RFLOAT range = minmax.second - minmax.first;
            const RFLOAT step = range / 255;

            gravis::tImage<gravis::bRGB> pngOut(Xsize(Iout()), Ysize(Iout()));
            pngOut.fill(gravis::bRGB(0));

            for (long int n = 0; n < Iout().size(); n++) {
                ColourScheme::grey_t val = floor((Iout()[n] - minmax.first) / step);
                ColourScheme::rgb_t rgb = color_scheme.greyToRGB(val);
                pngOut[n] = gravis::bRGB(rgb.r, rgb.g, rgb.b);
            }
            pngOut.writePNG(my_fn_out);
            #else
            REPORT_ERROR("You cannot write PNG images because libPNG was not linked during compilation.");
            #endif
        } else {
            if (n >= 0) {
                // This is a stack.
                // Assume the images in the stack are ordered.
                Iout.write(fn_tmp, n, true, n == 0 ? WRITE_OVERWRITE : WRITE_APPEND);
                // If n == 0, make a new stack.
            } else {
                Iout.write(my_fn_out);
            }
        }
    }

    void run() {
        my_new_box_size = -1;

        long int slice_id;
        std::string fn_stem;
        fn_in.decompose(slice_id, fn_stem);
        bool input_is_stack = (fn_in.getExtension() == "mrcs" || fn_in.getExtension() == "tif" || fn_in.getExtension() == "tiff") && slice_id == -1;
        bool input_is_star = fn_in.getExtension() == "star";
        // By default: write single output images

        // Get a MetaDataTable
        if (input_is_star) {
            do_ignore_optics = false;
            ObservationModel::loadSafely(fn_in, obsModel, MD, "discover", verb, false); // false means don't die upon failure
            if (obsModel.opticsMdt.empty()) {
                do_ignore_optics = true;
                std::cout << " + WARNING: reading input STAR file without optics groups ..." << std::endl;
                MD.read(fn_in);
            }
            if (fn_out.getExtension() != "mrcs")
                std::cout << "NOTE: the input (--i) is a STAR file but the output (--o) does not have .mrcs extension. "
                    << "The output is treated as a suffix, not a path." << std::endl;
            const FileName fn_img = MD.getValue<std::string>(EMDL::IMAGE_NAME, 0);
            fn_img.decompose(slice_id, fn_stem);
            input_is_stack = (fn_in.getExtension() == "mrcs" || fn_in.getExtension() == "tif" || fn_in.getExtension() == "tiff") && (slice_id == -1);
        } else if (input_is_stack) {
            if (bin_avg > 0 || avg_first >= 0 && avg_last >= 0) {
                MD.setValue(EMDL::IMAGE_NAME, fn_in, MD.addObject());
            } else {
                // Read the header to get the number of images inside the stack and generate that many lines in the MD
                auto tmp = Image<RFLOAT>::from_filename(fn_in, false);  // false means do not read image now, only header
                for (int i = 1; i <= Nsize(tmp()); i++) {
                    const auto fn_tmp = FileName::compose(i, fn_in);
                    MD.setValue(EMDL::IMAGE_NAME, fn_tmp, MD.addObject());
                }
            }
        } else {
            // Just individual image input
            MD.setValue(EMDL::IMAGE_NAME, fn_in, MD.addObject());
        }

        int i_img = 0;
        time_config();
        if (verb > 0)
            init_progress_bar(MD.size());

        bool do_md_out = false;
        for (long int i : MD) {
            FileName fn_img = MD.getValue<std::string>(do_average_all_frames ? EMDL::MICROGRAPH_MOVIE_NAME : EMDL::IMAGE_NAME, i);

            // For fourfilter...
            RFLOAT psi = maybe(0.0, [&] (){ return MD.getValue<RFLOAT>(EMDL::ORIENT_PSI, i); });

            Image<RFLOAT> Iin;
            // Initialise for the first image
            if (i_img == 0) {
                auto Ihead = Image<RFLOAT>::from_filename(fn_img, false);
                const auto dimensions = Ihead.getDimensions();
                xdim = dimensions.x;
                ydim = dimensions.y;
                zdim = dimensions.z;
                ndim = dimensions.n;

                if (zdim > 1 && (do_add_edge || do_flipXY || do_flipmXY))
                    REPORT_ERROR("ERROR: you cannot perform 2D operations "
                        "like --add_edge, --flipXY or --flipmXY on 3D maps. "
                        "If you intended to operate on a movie, use .mrcs extensions for stacks!");

                if (zdim > 1 && (bin_avg > 0 || avg_first >= 0 && avg_last >= 0))
                    REPORT_ERROR("ERROR: you cannot perform movie-averaging operations on 3D maps. If you intended to operate on a movie, use .mrcs extensions for stacks!");

                if (!fn_mult.empty()) {
                    Iop.read(fn_mult);
                } else if (!fn_div.empty()) {
                    Iop.read(fn_div);
                } else if (!fn_add.empty()) {
                    Iop.read(fn_add);
                } else if (!fn_subtract.empty()) {
                    Iop.read(fn_subtract);
                    if (do_optimise_scale_subtract && !fn_mask.empty())
                        Imask.read(fn_mask);
                } else if (!fn_fsc.empty()) {
                    Iop.read(fn_fsc);
                } else if (!fn_cosDPhi.empty()) {
                    Iop.read(fn_cosDPhi);
                } else if (!fn_adjust_power.empty()) {
                    Iop.read(fn_adjust_power);
                } else if (!fn_fourfilter.empty()) {
                    Iop.read(fn_fourfilter);
                } else if (!fn_correct_ampl.empty()) {
                    Iop.read(fn_correct_ampl);

                    // Calculate by the radial average in the Fourier domain
                    MultidimArray<RFLOAT> spectrum = MultidimArray<RFLOAT>::zeros(Ysize(Iop()));
                    MultidimArray<RFLOAT> count    = MultidimArray<RFLOAT>::zeros(Ysize(Iop()));
                    FOR_ALL_ELEMENTS_IN_FFTW_TRANSFORM(Iop()) {
                        long int idx = round(hypot2(ip, jp, kp));
                        spectrum.elem(idx) += direct::elem(Iop(), i, j, k);
                        count.elem(idx) += 1.0;
                    }
                    for (int i = Xinit(spectrum); i <= Xlast(spectrum); i++) {
                        if (count.elem(i) > 0.0)
                            spectrum.elem(i) /= count.elem(i);
                    }

                    FOR_ALL_ELEMENTS_IN_FFTW_TRANSFORM(Iop()) {
                        long int idx = round(hypot2(ip, jp, kp));
                        if (idx > minr_ampl_corr) {
                            direct::elem(Iop(), i, j, k) /= spectrum.elem(idx);
                        } else {
                            direct::elem(Iop(), i, j, k) = 1.0;
                        }
                    }
                    avg_ampl = Iop();
                    Iop.write("test.mrc");
                }

                if (!fn_mult.empty() || !fn_div.empty() || !fn_add.empty() || !fn_subtract.empty() || !fn_fsc.empty() || !fn_adjust_power.empty() || !fn_fourfilter.empty())
                    if (Xsize(Iop()) != xdim || Ysize(Iop()) != ydim || Zsize(Iop()) != zdim)
                        REPORT_ERROR("Error: operate-image is not of the correct size");

                if (do_avg_ampl || do_avg_ampl2 || do_avg_ampl2_ali) {
                    avg_ampl = MultidimArray<RFLOAT>::zeros(xdim / 2 + 1, ydim, zdim);
                } else if (do_average || do_average_all_frames) {
                    avg_ampl = MultidimArray<RFLOAT>::zeros(xdim, ydim, zdim);
                }
            }

            if (do_stats) {
                // only write statistics to screen
                Iin.read(fn_img);
                const auto stats = computeStats(Iin());
                RFLOAT header_angpix = Iin.samplingRateX();
                std::cout << fn_img << " : (x, y, z, n) = "
                    << Xsize(Iin()) << " × " << Ysize(Iin()) << " × "
                    << Zsize(Iin()) << " × " << Nsize(Iin()) << " ; "
                    "avg = " << stats.avg << " stddev = " << stats.stddev << " "
                    "minval = " << stats.min << " maxval = " << stats.max << "; "
                    "angpix = " << header_angpix << std::endl;
            } else if (do_calc_com) {
                Vector <RFLOAT> com (3);
                Iin.read(fn_img);
                Iin().setXmippOrigin().centerOfMass(com);
                std::cout << fn_img << " : center of mass (relative to XmippOrigin)"
                                                 " x " << XX(com);
                if (com.size() > 1) std::cout << " y " << YY(com);
                if (com.size() > 2) std::cout << " z " << ZZ(com);
                std::cout << std::endl;
            } else if (do_avg_ampl || do_avg_ampl2 || do_avg_ampl2_ali) {
                Iin.read(fn_img);

                if (do_avg_ampl2_ali) {
                    auto psi  = MD.getValue<RFLOAT>(EMDL::ORIENT_PSI,      i);
                    auto xoff = MD.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_X, i);
                    auto yoff = MD.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_Y, i);
                    // Apply the actual transformation
                    Matrix<RFLOAT> A = rotation2DMatrix(psi);
                    A.at(0, 2) = xoff;
                    A.at(1, 2) = yoff;
                    Iin() = applyGeometry(Iin(), A, IS_NOT_INV, DONT_WRAP);
                }

                MultidimArray<Complex> FT = transformer.FourierTransform(Iin());

                if (do_avg_ampl) {
                    for (long int n = 0; n < FT.size(); n++) {
                        avg_ampl[n] += abs(FT[n]);
                    }
                } else if (do_avg_ampl2 || do_avg_ampl2_ali) {
                    for (long int n = 0; n < FT.size(); n++) {
                        avg_ampl[n] += norm(FT[n]);
                    }
                }
            } else if (do_average) {
                Iin.read(fn_img);
                for (long int n = 0; n < Iin().size(); n++) {
                    avg_ampl[n] += Iin()[n];
                }
            } else if (do_average_all_frames) {
                Iin.read(fn_img);
                for (int n = 0; n < ndim; n++) {
                    for (long int k = 0; k < Zsize(avg_ampl); k++)
                    for (long int j = 0; j < Ysize(avg_ampl); j++)
                    for (long int i = 0; i < Xsize(avg_ampl); i++) {
                        direct::elem(avg_ampl, i, j, k) +=  direct::elem(Iin(), i, j, k, n);
                    }
                }
            } else if (bin_avg > 0 || avg_first >= 0 && avg_last >= 0) {
                // movie-frame averaging operations
                int avgndim = bin_avg > 0 ? ndim / bin_avg : 1;
                Image<RFLOAT> Iavg (xdim, ydim, zdim, avgndim);

                if (ndim == 1)
                    REPORT_ERROR("ERROR: you are trying to perform movie-averaging options on a single image/volume");

                FileName fn_ext = fn_out.getExtension();
                if (Nsize(Iavg()) > 1 && fn_ext.contains("mrc") && !fn_ext.contains("mrcs"))
                    REPORT_ERROR("ERROR: trying to write a stack into an MRC image. Use .mrcs extensions for stacks!");

                for (long int nn = 0; nn < ndim; nn++) {
                    Iin.read(fn_img, true, nn);
                    if (bin_avg > 0) {
                        int myframe = nn / bin_avg;
                        if (myframe < avgndim) {
                            for (long int j = 0; j < Ysize(Iin()); j++)
                            for (long int i = 0; i < Xsize(Iin()); i++) {
                                direct::elem(Iavg(), i, j, 0, myframe) += direct::elem(Iin(), i, j); // just store sum
                            }
                        }
                    } else if (avg_first >= 0 && avg_last >= 0 && nn + 1 >= avg_first && nn + 1 <= avg_last) {
                        //                                             ^ Start counting at 1
                        Iavg() += Iin(); // just store sum
                    }
                }
                Iavg.write(fn_out);
            } else {
                Iin.read(fn_img);
                FileName my_fn_out;

                if (fn_out.getExtension() == "mrcs" && !fn_out.contains("@")) {
                    my_fn_out = FileName::compose(i + 1, fn_out);
                } else {
                    if (input_is_stack) {
                        my_fn_out = fn_img.insertBeforeExtension("_" + fn_out);
                        long int dummy;
                        FileName fn_tmp;
                        my_fn_out.decompose(dummy, fn_tmp);
                        n_images[fn_tmp]++; // this is safe. see https://stackoverflow.com/questions/16177596/stdmapstring-int-default-initialization-of-value.
                        my_fn_out = FileName::compose(n_images[fn_tmp], fn_tmp);
                    } else if (input_is_star) {
                        my_fn_out = fn_img.insertBeforeExtension("_" + fn_out);
                    } else {
                        my_fn_out = fn_out;
                    }
                }
                perImageOperations(Iin, my_fn_out, psi);
                do_md_out = true;
                MD.setValue(EMDL::IMAGE_NAME, my_fn_out, MD.size() - 1);
            }

            i_img += ndim;
            if (verb > 0)
                progress_bar(i_img / ndim);
        }

        if (do_avg_ampl || do_avg_ampl2 || do_avg_ampl2_ali || do_average || do_average_all_frames) {
            avg_ampl /= (RFLOAT) i_img;
            Iout() = avg_ampl;
            Iout.write(fn_out);
        }

        if (verb > 0)
            progress_bar(MD.size());

        if (do_md_out && fn_in.getExtension() == "star") {
            const FileName fn_md_out = fn_in.insertBeforeExtension("_" + fn_out);

            if (do_ignore_optics) {
                MD.write(fn_md_out);
            } else {
                if (my_new_box_size > 0) {
                    for (long int i : obsModel.opticsMdt) {
                        obsModel.opticsMdt.setValue(EMDL::IMAGE_SIZE, my_new_box_size, i);
                    }
                }
                if (real_angpix > 0) {
                    for (long int i : obsModel.opticsMdt) {
                        obsModel.opticsMdt.setValue(EMDL::IMAGE_PIXEL_SIZE, real_angpix, i);
                    }
                }
                obsModel.save(MD, fn_md_out);
            }

            std::cout << " Written out new STAR file: " << fn_md_out << std::endl;
        }
    }
};

int main(int argc, char *argv[]) {
    image_handler_parameters prm;

    try {
        prm.read(argc, argv);
        prm.run();
    } catch (RelionError XE) {
        // prm.usage();
        std::cerr << XE;
        return RELION_EXIT_FAILURE;
    }
    return RELION_EXIT_SUCCESS;
}
