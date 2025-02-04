/***************************************************************************
 *
 * Authors: Sjors H.W. Scheres and Jasenko Zivanov
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

#include <src/backprojector.h>
#include <src/funcs.h>
#include <src/ctf.h>
#include <src/args.h>
#include <src/error.h>
#include <src/euler.h>
#include <src/time.h>
#include <omp.h>

#include <src/jaz/image_log.h>
#include <src/jaz/complex_io.h>
#include <src/jaz/stack_helper.h>
#include <src/jaz/img_proc/image_op.h>
#include <src/jaz/obs_model.h>
#include <src/jaz/new_ft.h>
#include <src/jaz/img_proc/filter_helper.h>
#include <src/jaz/ctf/delocalisation_helper.h>
#include "src/jaz/ctf_helper.h"

class reconstruct_parameters {

    public:

        FileName fn_out, fn_sel, fn_img, fn_sym, fn_sub;

        int r_max, r_min_nn, blob_order, ref_dim, interpolator, grid_iters,
            nr_omp_threads,
            nr_helical_asu, newbox, width_mask_edge, nr_sectors;

        RFLOAT blob_radius, blob_alpha, angular_error, shift_error,
            helical_rise, helical_twist;

        bool deloc_supp, ctf_phase_flipped, only_flip_phases, intact_ctf_first_peak,
            do_fom_weighting, do_3d_rot, do_ewald;

        bool skip_gridding, debug, do_reconstruct_meas, is_positive, read_weights, div_avg;

        bool wiener, writeWeights, new_Ewald_weight, Ewald_ellipsoid;

        float padding_factor, mask_diameter_ds, mask_diameter, mask_diameter_filt, flank_width;
        double padding_factor_2D;

        // I/O Parser
        IOParser parser;

        void usage() {
            parser.writeUsage(std::cerr);
        }

        void read(int argc, char **argv) {

            parser.setCommandLine(argc, argv);

            int general_section = parser.addSection("General options");
            fn_sel = parser.getOption("--i", "Input STAR file with the projection images and their orientations", "");
            fn_out = parser.getOption("--o", "Name for output reconstruction");
            fn_sym = parser.getOption("--sym", "Symmetry group", "c1");
            padding_factor = textToFloat(parser.getOption("--pad", "Padding factor", "2"));
            padding_factor_2D = textToDouble(parser.getOption("--pad2D", "Padding factor for 2D images", "1"));

            mask_diameter_filt = textToFloat(parser.getOption("--filter_diameter", "Diameter of filter-mask applied before division", "-1"));
            flank_width = textToFloat(parser.getOption("--filter_softness", "Width of filter-mask edge", "30"));
            nr_omp_threads = textToInteger(parser.getOption("--j", "Number of open-mp threads to use. Memory footprint is multiplied by this value.", "16"));

            int ctf_section = parser.addSection("CTF options");

            deloc_supp = parser.checkOption("--dm", "Apply delocalisation masking");
            mask_diameter_ds = textToDouble(parser.getOption("--mask_diameter_ds", "Diameter (in A) of mask for delocalisation suppression", "50"));
            intact_ctf_first_peak = parser.checkOption("--ctf_intact_first_peak", "Leave CTFs intact until first peak");
            ctf_phase_flipped = parser.checkOption("--ctf_phase_flipped", "Images have been phase flipped");
            only_flip_phases = parser.checkOption("--only_flip_phases", "Do not correct CTF-amplitudes, only flip phases");

            read_weights = parser.checkOption("--read_weights", "Read freq. weight files");
            writeWeights = parser.checkOption("--write_weights", "Write the weights volume");
            do_ewald = parser.checkOption("--ewald", "Correct for Ewald-sphere curvature (developmental)");
            mask_diameter  = textToFloat(parser.getOption("--mask_diameter", "Diameter (in A) of mask for Ewald-sphere curvature correction", "-1."));
            width_mask_edge = textToInteger(parser.getOption("--width_mask_edge", "Width (in pixels) of the soft edge on the mask", "3"));
            is_positive = !parser.checkOption("--reverse_curvature", "Try curvature the other way around");
            newbox = textToInteger(parser.getOption("--newbox", "Box size of reconstruction after Ewald sphere correction", "-1"));
            nr_sectors = textToInteger(parser.getOption("--sectors", "Number of sectors for Ewald sphere correction", "2"));

            int helical_section = parser.addSection("Helical options");
            nr_helical_asu = textToInteger(parser.getOption("--nr_helical_asu", "Number of helical asymmetrical units", "1"));
            helical_rise = textToFloat(parser.getOption("--helical_rise", "Helical rise (in Angstroms)", "0."));
            helical_twist = textToFloat(parser.getOption("--helical_twist", "Helical twist (in degrees, + for right-handedness)", "0."));

            int expert_section = parser.addSection("Expert options");
            fn_sub = parser.getOption("--subtract","Subtract projections of this map from the images used for reconstruction", "");
            wiener = !parser.checkOption("--legacy", "Use gridding instead of Wiener filter");
            new_Ewald_weight = parser.checkOption("--new_Ewald_weight", "Use Ewald weight W that considers Cs as well");
            Ewald_ellipsoid = parser.checkOption("--Ewald_ellipsoid", "Allow Ewald sphere to become an ellipsoid under aniso. mag.");

            if (parser.checkOption("--NN", "Use nearest-neighbour instead of linear interpolation before gridding correction")) {
                interpolator = NEAREST_NEIGHBOUR;
            } else {
                interpolator = TRILINEAR;
            }

            blob_radius   = textToFloat(parser.getOption("--blob_r", "Radius of blob for gridding interpolation", "1.9"));
            blob_order    = textToInteger(parser.getOption("--blob_m", "Order of blob for gridding interpolation", "0"));
            blob_alpha    = textToFloat(parser.getOption("--blob_a", "Alpha-value of blob for gridding interpolation", "15"));
            grid_iters = textToInteger(parser.getOption("--iter", "Number of gridding-correction iterations", "10"));
            ref_dim = textToInteger(parser.getOption("--refdim", "Dimension of the reconstruction (2D or 3D)", "3"));
            angular_error = textToFloat(parser.getOption("--angular_error", "Apply random deviations with this standard deviation (in degrees) to each of the 3 Euler angles", "0."));
            shift_error = textToFloat(parser.getOption("--shift_error", "Apply random deviations with this standard deviation (in pixels) to each of the 2 translations", "0."));
            do_fom_weighting = parser.checkOption("--fom_weighting", "Weight particles according to their figure-of-merit (_rlnParticleFigureOfMerit)");
            do_3d_rot = parser.checkOption("--3d_rot", "Perform 3D rotations instead of backprojections from 2D images");
            skip_gridding = !parser.checkOption("--grid", "Perform gridding part of the reconstruction");
            div_avg = parser.checkOption("--div_avg", "Divide the per-voxel average by its weight prior to computing the preliminary FSC");

            debug = parser.checkOption("--debug", "Write out debugging data");

            // Hidden
            r_min_nn = textToInteger(getParameter(argc, argv, "--r_min_nn", "10"));

            // Check for errors in the command-line option
            if (parser.checkForErrors()) {
                REPORT_ERROR("Errors encountered on the command line (see above). Exiting...");
            }
        }

        void applyCTFPandCTFQ(
            MultidimArray<Complex> &Fin, CTF &ctf, FourierTransformer &transformer,
            MultidimArray<Complex> &outP, MultidimArray<Complex> &outQ, double angpix
        ) {
            // FourierTransformer transformer;
            outP.resize(Fin);
            outQ.resize(Fin);
            float angle_step = 180.0 / nr_sectors;
            for (float angle = 0.0; angle < 180.0;  angle += angle_step) {
                MultidimArray<Complex> CTFP(Fin), Fapp(Fin);
                MultidimArray<RFLOAT> Iapp(Ysize(Fin), Ysize(Fin));
                // Two passes: one for CTFP, one for CTFQ
                for (int ipass = 0; ipass < 2; ipass++) {
                    bool is_my_positive = (ipass == 1) ? is_positive : !is_positive;

                    // Get CTFP and multiply the Fapp with it
                    CTFP = ctf.getCTFPImage(Fin.xdim, Fin.ydim, Ysize(Fin), Ysize(Fin), angpix, is_my_positive, angle);

                    Fapp = Fin * CTFP; // Element-wise complex multiplication

                    // Inverse Fourier transform and mask out the particle
                    Iapp = transformer.inverseFourierTransform(Fapp);
                    CenterFFT(Iapp, -1);

                    softMaskOutsideMap(Iapp, round(mask_diameter / (angpix * 2.0)), (RFLOAT) width_mask_edge);

                    // Re-box to a smaller size if necessary....
                    if (0 < newbox && newbox < Ysize(Fin)) {
                        Iapp = Iapp.setXmippOrigin().windowed(
                            Xmipp::init(newbox), Xmipp::last(newbox),
                            Xmipp::init(newbox), Xmipp::last(newbox));
                    }

                    // Back into Fourier-space
                    CenterFFT(Iapp, +1);
                    Fapp = transformer.FourierTransform(Iapp);  // std::move?

                    // First time round: resize the output arrays
                    if (ipass == 0 && fabs(angle) < Xmipp::epsilon) {
                        outP.resize(Fapp);
                        outQ.resize(Fapp);
                    }

                    // Now set back the right parts into outP (first pass) or outQ (second pass)
                    float anglemin = angle + 90.0 - 0.5 * angle_step;
                    float anglemax = angle + 90.0 + 0.5 * angle_step;

                    // angles larger than 180
                    bool is_reverse = false;
                    if (anglemin >= 180.0) {
                        anglemin -= 180.0;
                        anglemax -= 180.0;
                        is_reverse = true;
                    }

                    bool PorQoutP = is_angle_reverse != (ipass == 0);
                    auto &myCTFPorQ  = PorQoutP ? outP : outQ;
                    auto &myCTFPorQb = PorQoutP ? outQ : outP;

                    // Deal with sectors with the Y-axis in the middle of the sector...
                    bool do_wrap_max = false;
                    if (anglemin < 180.0 && anglemax > 180.0) {
                        anglemax -= 180.0;
                        do_wrap_max = true;
                    }

                    // Convert to radians
                    anglemin = radians(anglemin);
                    anglemax = radians(anglemax);
                    FOR_ALL_ELEMENTS_IN_FFTW_TRANSFORM2D(CTFP) {
                        const RFLOAT theta  = atan2(ip, jp);
                        // Only take the relevant sector now...
                        if (do_wrap_max) {
                            if (theta >= anglemin) {
                                direct::elem(myCTFPorQ, i, j) = direct::elem(Fapp, i, j);
                            } else if (theta < anglemax) {
                                direct::elem(myCTFPorQb, i, j) = direct::elem(Fapp, i, j);
                            }
                        } else {
                            if (theta >= anglemin && theta < anglemax) {
                                direct::elem(myCTFPorQ, i, j) = direct::elem(Fapp, i, j);
                            }
                        }
                    }
                }
            }
        }

        void reconstruct() {
            int data_dim = do_3d_rot ? 3 : 2;

            MultidimArray<RFLOAT> dummy;
            Image<RFLOAT> vol, sub;

            ObservationModel obsModel;
            MetaDataTable mdt0;

            ObservationModel::loadSafely(fn_sel, obsModel, mdt0);
            std::vector<double> angpix = obsModel.getPixelSizes();

            const int optGroupCount = obsModel.numberOfOpticsGroups();

            // Use pixel and box size of first opt. group for output;
            double angpixOut = angpix[0];
            int boxOut;

            // When doing Ewald-curvature correction: allow reconstructing smaller
            // box than the input images (which should have large boxes!!)
            if (do_ewald && newbox > 0) {
                boxOut = newbox;
            } else {
                boxOut = obsModel.getBoxSize(0);
            }

            std::vector<int> paddedSizes2D(optGroupCount);
            std::vector<int> origSizes2D(optGroupCount);

            for (int i = 0; i < optGroupCount; i++) {
                paddedSizes2D[i] = (int) (padding_factor_2D * obsModel.getBoxSize(i));
                origSizes2D[i] = (int) obsModel.getBoxSize(i);
            }

            // Get dimension of the images

            fn_img = mdt0.getValue<FileName>(EMDL::IMAGE_NAME, 0);

            Projector subProjector(sub.data.xdim, interpolator, padding_factor, r_min_nn);

            r_max = -1;

            if (!fn_sub.empty()) {
                sub.read(fn_sub);
                subProjector.computeFourierTransformMap(sub(), dummy, 2 * r_max);
            }

            std::vector<MetaDataTable> mdts = StackHelper::splitByStack(mdt0);
            const long gc = mdts.size();

            std::vector<Image<RFLOAT>> prevRefs(2);
            std::vector<std::vector<BackProjector>> backprojectors(2);

            for (int j = 0; j < 2; j++) {
                backprojectors[j] = std::vector<BackProjector>(nr_omp_threads);

                for (int i = 0; i < nr_omp_threads; i++) {
                    backprojectors[j][i] = BackProjector(
                        boxOut, ref_dim, fn_sym, interpolator,
                        padding_factor, r_min_nn, blob_order,
                        blob_radius, blob_alpha, data_dim, skip_gridding
                    );
                }
            }

            std::cout << "Back-projecting all images ..." << std::endl;

            time_config();
            init_progress_bar(gc / nr_omp_threads);


            #pragma omp parallel num_threads(nr_omp_threads)
            {
                int threadnum = omp_get_thread_num();

                backprojectors[0][threadnum].initZeros(2 * r_max);
                backprojectors[1][threadnum].initZeros(2 * r_max);

                RFLOAT rot, tilt, psi, fom, r_ewald_sphere;
                FourierTransformer transformer;

                #pragma omp for
                for (int g = 0; g < gc; g++) {
                    std::vector<Image<RFLOAT>> obsR;

                    MetaDataTable table = mdts[g];

                    try {
                        obsR = StackHelper::loadStack(table);
                    } catch (RelionError XE) {
                        std::cerr << "warning: unable to load micrograph #" << (g + 1) << "\n";
                        continue;
                    }

                    const long pc = obsR.size();

                    for (int p = 0; p < pc; p++) {
                        int randSubset = table.getValue<int>(EMDL::PARTICLE_RANDOM_SUBSET, p) - 1;

                        // Rotations
                        if (ref_dim == 2) {
                            rot = tilt = 0.0;
                        } else {
                            rot  = table.getValue<RFLOAT>(EMDL::ORIENT_ROT,  p);
                            tilt = table.getValue<RFLOAT>(EMDL::ORIENT_TILT, p);
                        }

                        psi = 0.0;
                        psi = table.getValue<RFLOAT>(EMDL::ORIENT_PSI, p);

                        if (angular_error > 0.0) {
                            rot  += rnd_gaus(0.0, angular_error);
                            tilt += rnd_gaus(0.0, angular_error);
                            psi  += rnd_gaus(0.0, angular_error);
                            //std::cout << rnd_gaus(0.0, angular_error) << std::endl;
                        }

                        Matrix<RFLOAT> A3D = Euler::angles2matrix(rot, tilt, psi);

                        int opticsGroup = obsModel.getOpticsGroup(table, p);
                        double pixelsize = angpix[opticsGroup];

                        // If we are considering Ewald sphere curvature, the mag. matrix
                        // has to be provided to the backprojector explicitly
                        // (to avoid creating an Ewald ellipsoid)
                        if ((!do_ewald || Ewald_ellipsoid) && obsModel.hasMagMatrices) {
                            A3D = A3D.matmul(obsModel.anisoMag(opticsGroup));
                        }

                        A3D *= obsModel.scaleDifference(opticsGroup, boxOut, angpixOut);
                        A3D /= padding_factor_2D;

                        // Translations (either through phase-shifts or in real space
                        Vector<RFLOAT> trans (2 + do_3d_rot);
                        std::fill(trans.begin(), trans.end(), 0);
                        XX(trans) = table.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_X_ANGSTROM, p) / pixelsize;
                        YY(trans) = table.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_Y_ANGSTROM, p) / pixelsize;

                        if (shift_error > 0.0) {
                            XX(trans) += rnd_gaus(0.0, shift_error);
                            YY(trans) += rnd_gaus(0.0, shift_error);
                        }

                        if (do_3d_rot) {
                            ZZ(trans) = table.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_Z, p);

                            if (shift_error > 0.0) {
                                ZZ(trans) += rnd_gaus(0.0, shift_error);
                            }
                        }

                        if (do_fom_weighting) {
                            fom = table.getValue<RFLOAT>(EMDL::PARTICLE_FOM, p);
                        }

                        CenterFFT(obsR[p](), +1);

                        const int sPad2D = paddedSizes2D[opticsGroup];

                        if (padding_factor_2D > 1.0) {
                            obsR[p] = FilterHelper::padCorner2D(obsR[p], sPad2D, sPad2D);
                        }

                        MultidimArray<Complex> F2D = transformer.FourierTransform(obsR[p]());

                        if (abs(XX(trans)) > 0.0 || abs(YY(trans)) > 0.0)
                            shiftImageInFourierTransform(
                                F2D, sPad2D, XX(trans), YY(trans), do_3d_rot ? ZZ(trans) : 0.0
                            );

                        auto Fctf = MultidimArray<RFLOAT>::ones(F2D.xdim, F2D.ydim, F2D.zdim, F2D.ndim);

                        CTF ctf = CtfHelper::make_CTF(table, &obsModel, p);

                        Fctf = CtfHelper::getFftwImage(
                            ctf,
                            Xsize(Fctf), Ysize(Fctf), sPad2D, sPad2D, pixelsize,
                            &obsModel,
                            ctf_phase_flipped, only_flip_phases,
                            intact_ctf_first_peak, true
                        );

                        if (deloc_supp) {
                            DelocalisationHelper::maskOutsideBox(
                                ctf, &obsModel, , mask_diameter_ds / (pixelsize * 2.0),
                                pixelsize, origSizes2D[opticsGroup],
                                Fctf, XX(trans), YY(trans)
                            );
                        }

                        if (p) {
                            obsModel.modulatePhase(table, F2D);
                            obsModel.multiplyByMtf(table, F2D);
                        } else {
                            obsModel.demodulatePhase(table, F2D);
                            obsModel.divideByMtf    (table, F2D);
                        }

                        MultidimArray<Complex> F2DP, F2DQ;
                        if (do_ewald) {
                            // Ewald-sphere curvature correction
                            applyCTFPandCTFQ(F2D, ctf, transformer, F2DP, F2DQ, pixelsize);

                            // Also calculate W, store again in Fctf
                            (new_Ewald_weight ? CtfHelper::applyWeightEwaldSphereCurvature_new
                                              : CtfHelper::applyWeightEwaldSphereCurvature)
                            (ctf, sPad2D, sPad2D, angpix[opticsGroup], mask_diameter);  // IIFE

                            // Also calculate the radius of the Ewald sphere (in pixels)
                            r_ewald_sphere = boxOut * angpix[opticsGroup] / ctf.lambda;
                        }

                        BackProjector backproj = backprojectors[randSubset][threadnum];

                        // Subtract reference projection
                        if (!fn_sub.empty()) {

                            F2D -= obsModel.predictObservation(
                                subProjector, table, p, true, true, true
                            );

                            // Back-project difference image
                            backproj.set2DFourierTransform(F2D, A3D);
                        } else {
                            if (do_ewald) {
                                Fctf *= Fctf;
                            } else {
                                // "Normal" reconstruction, multiply X by CTF, and W by CTF^2
                                for (long int n = 0; n < F2D.size(); n++) {
                                    F2D[n]  *= Fctf[n];
                                    Fctf[n] *= Fctf[n];
                                }
                            }

                            // Do the following after squaring the CTFs!
                            if (do_fom_weighting) {
                                for (long int n = 0; n < F2D.size(); n++) {
                                    F2D[n]  *= fom;
                                    Fctf[n] *= fom;
                                }
                            }

                            direct::elem(F2D, 0, 0) = 0.0;

                            if (do_ewald) {

                                Matrix<RFLOAT> magMat = obsModel.hasMagMatrices && !Ewald_ellipsoid ?
                                    obsModel.getMagMatrix(opticsGroup) :
                                    Matrix<RFLOAT>::identity(2);

                                backproj.set2DFourierTransform(
                                    F2DP, A3D, &Fctf, r_ewald_sphere, true, &magMat
                                );
                                backproj.set2DFourierTransform(
                                    F2DQ, A3D, &Fctf, r_ewald_sphere, false, &magMat
                                );
                            } else {
                                backproj.set2DFourierTransform(F2D, A3D, &Fctf);
                            }
                        }

                        if (threadnum == 0) {
                            progress_bar(g);
                        }
                    }
                }
            }

            progress_bar(gc / nr_omp_threads);

            std::vector<BackProjector*> backprojector(2);

            for (int j = 0; j < 2; j++) {
                std::cerr << " + Merging volumes for half-set " << j + 1 << "...\n";

                backprojector[j] = &backprojectors[j][0];

                for (int bpi = 1; bpi < nr_omp_threads; bpi++) {
                    auto &bp = backprojectors[j][bpi];
                    backprojector[j]->data += bp.data;
                    bp.data.clear();

                    backprojector[j]->weight += bp.weight;
                    bp.weight.clear();
                }

                std::cerr << " + Symmetrising half-set " << j + 1 << "...\n";

                backprojector[j]->symmetrise(
                    nr_helical_asu, helical_twist, helical_rise / angpixOut, nr_omp_threads
                );
            }

            bool do_map = wiener;
            bool do_use_fsc = wiener;

            MultidimArray<RFLOAT> fsc (boxOut / 2 + 1);

            if (wiener) {
                const MultidimArray<Complex> avg0 = backprojector[0]->getDownsampledAverage(div_avg);
                const MultidimArray<Complex> avg1 = backprojector[1]->getDownsampledAverage(div_avg);
                fsc = backprojector[0]->calculateDownSampledFourierShellCorrelation(avg0, avg1);
            }

            if (debug) {
                std::ofstream fscNew(fn_out + "_prelim_FSC.dat");

                for (int i = 0; i < fsc.xdim; i++) {
                    fscNew << i << " " << fsc(i) << "\n";
                }
            }

            // Two halves
            for (int j = 0; j < 2; j++) {
                if (mask_diameter_filt > 0.0) {
                    std::cout << " + Applying spherical mask of diameter "
                    << mask_diameter_filt << " ..." << std::endl;

                    const double r0 = mask_diameter_filt/2.0;
                    const double r1 = r0 + flank_width;

                    Image<Complex> tempC;
                    Image<RFLOAT> tempR;

                    BackProjector::decenterWhole(backprojector[j]->data, tempC());
                    NewFFT::inverseFourierTransform(tempC(), tempR(), NewFFT::FwdOnly, false);
                    tempR = FilterHelper::raisedCosEnvCorner3D(tempR, r0, r1);
                    NewFFT::FourierTransform(tempR(), tempC(), NewFFT::FwdOnly);
                    BackProjector::recenterWhole(tempC(), backprojector[j]->data);

                    BackProjector::decenterWhole(backprojector[j]->weight, tempC());
                    NewFFT::inverseFourierTransform(tempC(), tempR(), NewFFT::FwdOnly, false);
                    tempR = FilterHelper::raisedCosEnvCorner3D(tempR, r0, r1);
                    NewFFT::FourierTransform(tempR(), tempC(), NewFFT::FwdOnly);
                    BackProjector::recenterWhole(tempC(), backprojector[j]->weight);
                }

                std::cout << " + Starting the reconstruction ..." << std::endl;

                MultidimArray<RFLOAT> tau2;
                if (do_use_fsc)
                    backprojector[j]->updateSSNRarrays(1.0, tau2, dummy, dummy, dummy, fsc, do_use_fsc, true);

                Image<RFLOAT> *weights = writeWeights ? new Image<RFLOAT> : nullptr;
                vol() = backprojector[j]->reconstruct(
                    grid_iters, do_map, tau2,
                    1.0, 1.0, -1, false, weights
                );

                if (writeWeights) {
                    weights->write(fn_out + "_half" + std::to_string(j + 1) + "_class001_unfil_weight.mrc");
                    delete weights;
                }

                prevRefs[j] = vol;

            }

            // Write each half to a .mrc file
            for (int j = 0; j < 2; j++) {
                const std::string fnFull = fn_out + "_half" + std::to_string(j + 1) + "_class001_unfil.mrc";

                prevRefs[j].write(fnFull);
                std::cout << " Done writing map in " << fnFull << "\n";
            }
        }
};


int main(int argc, char *argv[]) {
    reconstruct_parameters prm;

    try {
        prm.read(argc, argv);
        prm.reconstruct();
    } catch (RelionError XE) {
        // prm.usage();
        std::cerr << XE;
        return RELION_EXIT_FAILURE;
    }
    return RELION_EXIT_SUCCESS;
}
