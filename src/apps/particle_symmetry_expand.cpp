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

#include <src/args.h>
#include <src/metadata_table.h>
#include <src/symmetries.h>
#include <src/euler.h>
#include <src/time.h>
#include <src/jaz/obs_model.h>

class particle_symmetry_expand_parameters {

    public:

    FileName fn_sym, fn_in, fn_out;

    // Helical symmetry
    bool do_helix, do_ignore_optics;
    RFLOAT twist, rise, angpix;
    int nr_asu, frac_sampling;
    RFLOAT frac_range;
    ObservationModel obsModel;

    // I/O Parser
    IOParser parser;


    void usage() {
        parser.writeUsage(std::cerr);
    }

    void read(int argc, char **argv) {
        parser.setCommandLine(argc, argv);

        int general_section = parser.addSection("Options");

        fn_in = parser.getOption("--i", "Input particle STAR file");
        fn_out = parser.getOption("--o", "Output expanded particle STAR file", "expanded.star");
        fn_sym = parser.getOption("--sym", "Symmetry point group", "C1");

        // Helical symmetry
        int helical_section = parser.addSection("Helix");
        do_helix = parser.checkOption("--helix", "Do helical symmetry expansion");
        twist = textToFloat(parser.getOption("--twist", "Helical twist (deg)", "0."));
        rise = textToFloat(parser.getOption("--rise", "Helical rise (A)", "0."));
        angpix = textToFloat(parser.getOption("--angpix", "Pixel size (A)", "1."));
        nr_asu = textToFloat(parser.getOption("--asu", "Number of asymmetrical units to expand", "1"));
        frac_sampling = textToFloat(parser.getOption("--frac_sampling", "Number of samplings in between a single asymmetrical unit", "1"));
        frac_range = textToFloat(parser.getOption("--frac_range", "Range of the rise [-0.5, 0.5> to be sampled", "0.5"));
        do_ignore_optics = parser.checkOption("--ignore_optics", "Provide this option for relion-3.0 functionality, without optics groups");

        // Check for errors in the command-line option
        if (parser.checkForErrors())
            REPORT_ERROR("Errors encountered on the command line (see above), exiting...");

        if (do_helix) {
            if (fn_sym != "C1")
                REPORT_ERROR("Provide either --sym OR --helix, but not both!");

            if ((nr_asu > 1 && frac_sampling > 1) || (nr_asu == 1 && frac_sampling == 1))
                REPORT_ERROR("Provide either --asu OR --frac_sampling, but not both!");
        }
    }

    void run() {
        MetaDataTable DFi, DFo;
        Matrix<RFLOAT> L (3, 3), R (3, 3); // A matrix from the list
        RFLOAT z_start, z_stop, z_step; // for helices
        SymList SL;

        // For helices, pre-calculate expansion range
        if (do_helix) {
            if (nr_asu > 1) {
                // Z_start and z_stop and z_step are in fractions of the rise!
                int istart = -(nr_asu - 1) / 2;
                int istop = nr_asu / 2;
                z_start = (RFLOAT)istart;
                z_stop  = (RFLOAT)istop;
                z_step  = 1.;
            } else if (frac_sampling > 1) {
                z_start = -frac_range;
                z_stop = (frac_range - 0.001);
                z_step = 1.0 / frac_sampling;
            }
            std::cout << " Helical: z_start= " << z_start << " z_stop= " << z_stop << " z_step= " << z_step << std::endl;
        } else {
            SL.read_sym_file(fn_sym);
            if (SL.SymsNo() < 1)
                REPORT_ERROR("ERROR Nothing to do. Provide a point group with symmetry!");
        }

        if (do_ignore_optics) {
            DFi.read(fn_in);
        } else {
            ObservationModel::loadSafely(fn_in, obsModel, DFi, "particles", 1, false);
            if (obsModel.opticsMdt.empty()) {
                std::cerr << " + WARNGING: could not read optics groups table, proceeding without it ..." << std::endl;
                DFi.read(fn_in);
                do_ignore_optics = true;
            }
        }

        int barstep = std::max(1, (int) DFi.size() / 60);
        init_progress_bar(DFi.size());
        DFo.clear();

        // Loop over input MetadataTable
        for (long int imgno : DFi) {

            RFLOAT rotp, tiltp, psip;

            RFLOAT rot  = DFi.getValue<RFLOAT>(EMDL::ORIENT_ROT,  imgno);
            RFLOAT tilt = DFi.getValue<RFLOAT>(EMDL::ORIENT_TILT, imgno);
            RFLOAT psi  = DFi.getValue<RFLOAT>(EMDL::ORIENT_PSI,  imgno);
            RFLOAT x    = DFi.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_X_ANGSTROM, imgno);
            RFLOAT y    = DFi.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_Y_ANGSTROM, imgno);

            if (do_helix) {
                for (RFLOAT z_pos = z_start; z_pos <= z_stop; z_pos += z_step) {
                    // TMP
                    /* if (fabs(z_pos) > 0.01) */ {
                        // Translation along the X-axis in the rotated image is along the helical axis in 3D.
                        // Tilted images shift less: sin(tilt)
                        RFLOAT xxt = z_pos * rise * sin(radians(tilt));
                        RFLOAT xp = x + xxt * cos(radians(-psi));
                        RFLOAT yp = y + xxt * sin(radians(-psi));
                        rotp = rot - z_pos * twist;
                        const long int i = DFo.addObject();
                        DFo.setObject(DFi.getObject(imgno), i);
                        DFo.setValue(EMDL::ORIENT_ROT, rotp, i);
                        DFo.setValue(EMDL::ORIENT_ORIGIN_X_ANGSTROM, xp, i);
                        DFo.setValue(EMDL::ORIENT_ORIGIN_Y_ANGSTROM, yp, i);
                    }
                }
            } else {
                // Get the original line from the STAR file
                const long int i = DFo.addObject();
                DFo.setObject(DFi.getObject(imgno), i);
                // And loop over all symmetry mates
                for (int isym = 0; isym < SL.SymsNo(); isym++) {
                    SL.get_matrices(isym, L, R);
                    L.resize(3, 3); // Erase last row and column
                    R.resize(3, 3); // as only the relative orientation is useful and not the translation
                    angles_t angles = Euler::apply_transf(L, R, rot, tilt, psi);
                    const long int i = DFo.addObject();
                    DFo.setObject(DFi.getObject(imgno), i);
                    DFo.setValue(EMDL::ORIENT_ROT,  angles.rot,  i);
                    DFo.setValue(EMDL::ORIENT_TILT, angles.tilt, i);
                    DFo.setValue(EMDL::ORIENT_PSI,  angles.psi,  i);
                }
            }

            if (imgno % barstep == 0) progress_bar(imgno);

        }
        progress_bar(DFi.size());


        if (do_ignore_optics) {
            DFo.write(fn_out);
        } else {
            obsModel.save(DFo, fn_out, "particles");
        }
        std::cout << " Done! Written: " << fn_out << " with the expanded particle set." << std::endl;

    }// end run function
};

int main(int argc, char *argv[]) {
    time_config();
    particle_symmetry_expand_parameters prm;

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
