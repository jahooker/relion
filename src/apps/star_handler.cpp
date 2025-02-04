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
#include <src/metadata_table.h>
#include <src/filename.h>
#include <src/time.h>
#include <src/jaz/obs_model.h>
#include <src/pipeline_jobs.h>
#include <cmath>
#include "src/plot_metadata.h"

class star_handler_parameters {

    public:

    FileName fn_in, tablename_in, fn_out, fn_compare, tablename_compare,
             fn_label1, fn_label2, fn_label3,
             select_label, select_str_label, discard_label,
             fn_check, fn_operate, fn_operate2, fn_operate3, fn_set;

    std::string remove_col_label, add_col_label, add_col_value, add_col_from, hist_col_label, select_include_str, select_exclude_str;
    RFLOAT eps, select_minval, select_maxval, multiply_by, add_to, center_X, center_Y, center_Z, hist_min, hist_max;
    bool do_ignore_optics, do_combine, do_split, do_center, do_random_order, show_frac, show_cumulative, do_discard;
    long int nr_split, size_split, nr_bin, random_seed;
    RFLOAT discard_sigma, duplicate_threshold, extract_angpix, cl_angpix;
    ObservationModel obsModel;
    // I/O Parser
    IOParser parser;

    void usage() { parser.writeUsage(std::cerr); }

    void read(int argc, char **argv) {

        parser.setCommandLine(argc, argv);

        int general_section = parser.addSection("General options");
        fn_in = parser.getOption("--i", "Input STAR file");
        fn_out = parser.getOption("--o", "Output STAR file", "out.star");
        do_ignore_optics = parser.checkOption("--ignore_optics", "Provide this option for relion-3.0 functionality, without optics groups");
        cl_angpix = textToFloat(parser.getOption("--angpix", "Pixel size in Angstrom, for when ignoring the optics groups in the input star file", "1."));
        tablename_in = parser.getOption("--i_tablename", "If ignoring optics, then read table with this name", "");

        int compare_section = parser.addSection("Compare options");
        fn_compare = parser.getOption("--compare", "STAR file name to compare the input STAR file with", "");
        fn_label1 = parser.getOption("--label1", "1st metadata label for the comparison (may be string, int or RFLOAT)", "");
        fn_label2 = parser.getOption("--label2", "2nd metadata label for the comparison (RFLOAT only) for 2D/3D-distance)", "");
        fn_label3 = parser.getOption("--label3", "3rd metadata label for the comparison (RFLOAT only) for 3D-distance)", "");
        eps = textToFloat(parser.getOption("--max_dist", "Maximum distance to consider a match (for int and RFLOAT only)", "0."));

        int subset_section = parser.addSection("Select options");
        select_label = parser.getOption("--select", "Metadata label (number) to base output selection on (e.g. rlnCtfFigureOfMerit)", "");
        select_minval = textToFloat(parser.getOption("--minval", "Minimum acceptable value for this label (inclusive)", "-99999999."));
        select_maxval = textToFloat(parser.getOption("--maxval", "Maximum acceptable value for this label (inclusive)", "99999999."));
        select_str_label = parser.getOption("--select_by_str", "Metadata label (string) to base output selection on (e.g. rlnMicrographname)", "");
        select_include_str = parser.getOption("--select_include", "select rows that contains this string in --select_by_str ", "");
        select_exclude_str = parser.getOption("--select_exclude", "exclude rows that contains this string in --select_by_str ", "");

        int discard_section = parser.addSection("Discard based on image statistics options");
        do_discard = parser.checkOption("--discard_on_stats", "Discard images if their average/stddev deviates too many sigma from the ensemble average");
        discard_label = parser.getOption("--discard_label", "MetaDataLabel that points to the images to be used for discarding based on statistics", "rlnImageName");
        discard_sigma = textToFloat(parser.getOption("--discard_sigma", "Discard images with average or stddev values that lie this many sigma away from the ensemble average", "4."));

        int combine_section = parser.addSection("Combine options");
        do_combine = parser.checkOption("--combine", "Combine input STAR files (multiple individual filenames, all within double-quotes after --i)");
        fn_check = parser.getOption("--check_duplicates", "MetaDataLabel (for a string only!) to check for duplicates, e.g. rlnImageName", "");

        int split_section = parser.addSection("Split options");
        do_split = parser.checkOption("--split", "Split the input STAR file into one or more smaller output STAR files");
        do_random_order = parser.checkOption("--random_order", "Perform splits on randomised order of the input STAR file");
        random_seed = textToInteger(parser.getOption("--random_seed", "Random seed for randomisation.", "-1"));
        nr_split = textToInteger(parser.getOption("--nr_split", "Split into this many equal-sized STAR files", "-1"));
        size_split = textToLongLong(parser.getOption("--size_split", "AND/OR split into subsets of this many lines", "-1"));

        int operate_section = parser.addSection("Operate options");
        fn_operate = parser.getOption("--operate", "Operate on this metadata label", "");
        fn_operate2 = parser.getOption("--operate2", "Operate also on this metadata label", "");
        fn_operate3 = parser.getOption("--operate3", "Operate also on this metadata label", "");
        fn_set = parser.getOption("--set_to", "Set all the values for the --operate label(s) to this value", "");
        multiply_by = textToFloat(parser.getOption("--multiply_by", "Multiply all the values for the --operate label(s) by this value", "1."));
        add_to = textToFloat(parser.getOption("--add_to", "Add this value to all the values for the --operate label(s)", "0."));

        int center_section = parser.addSection("Center options");
        do_center = parser.checkOption("--center", "Perform centering of particles according to a position in the reference.");
        center_X = textToFloat(parser.getOption("--center_X", "X-coordinate in the reference to center particles on (in pix)", "0."));
        center_Y = textToFloat(parser.getOption("--center_Y", "Y-coordinate in the reference to center particles on (in pix)", "0."));
        center_Z = textToFloat(parser.getOption("--center_Z", "Z-coordinate in the reference to center particles on (in pix)", "0."));

        int column_section = parser.addSection("Column options");
        remove_col_label = parser.getOption("--remove_column", "Remove the column with this metadata label from the input STAR file.", "");
        add_col_label = parser.getOption("--add_column", "Add a column with this metadata label from the input STAR file.", "");
        add_col_value = parser.getOption("--add_column_value", "Set this value in all rows for the added column", "");
        add_col_from = parser.getOption("--copy_column_from", "Copy values in this column to the added column", "");
        hist_col_label = parser.getOption("--hist_column", "Calculate histogram of values in the column with this metadata label", "");
        show_frac = parser.checkOption("--in_percent", "Show a histogram in percent (need --hist_column)");
        show_cumulative = parser.checkOption("--show_cumulative", "Show a histogram of cumulative distribution (need --hist_column)");
        nr_bin = textToInteger(parser.getOption("--hist_bins", "Number of bins for the histogram. By default, determined automatically by Freedman–Diaconis rule.", "-1"));
        hist_min = textToFloat(parser.getOption("--hist_min", "Minimum value for the histogram (needs --hist_bins)", "-inf"));
        hist_max = textToFloat(parser.getOption("--hist_max", "Maximum value for the histogram (needs --hist_bins)", "inf"));

        int duplicate_section = parser.addSection("Duplicate removal");
        duplicate_threshold = textToFloat(parser.getOption("--remove_duplicates","Remove duplicated particles within this distance [Angstrom]. Negative values disable this.", "-1"));
        extract_angpix = textToFloat(parser.getOption("--image_angpix", "For down-sampled particles, specify the pixel size [A/pix] of the original images used in the Extract job", "-1"));

        // Check for errors in the command-line option
        if (parser.checkForErrors())
            REPORT_ERROR("Errors encountered on the command line, exiting...");
    }

    void run() {
        int c = 0
              + !fn_compare.empty()
              + !select_label.empty()
              + !select_str_label.empty()
              + do_discard
              + do_combine
              + do_split
              + !fn_operate.empty()
              + do_center
              + !remove_col_label.empty()
              + !add_col_label.empty()
              + !hist_col_label.empty()
              + duplicate_threshold > 0;
        if (c != 1) {
            MetaDataTable MD = read_check_ignore_optics(fn_in);
            write_check_ignore_optics(MD, fn_out, MD.name);
            //REPORT_ERROR("ERROR: specify (only and at least) one of the following options: --compare, --select, --select_by_str, --combine, --split, --operate, --center, --remove_column, --add_column, --hist_column or --remove_duplicates.");
        }

        if (fn_out.empty() && hist_col_label.empty())
            REPORT_ERROR("ERROR: specify the output file name (--o)");

        if (!fn_compare.empty()) compare();
        if (!select_label.empty()) select();
        if (!select_str_label.empty()) select_by_str();
        if (do_discard) discard_on_image_stats();
        if (do_combine) combine();
        if (do_split) split();
        if (!fn_operate.empty()) operate();
        if (do_center) center();
        if (!remove_col_label.empty()) remove_column();
        if (!add_col_label.empty()) add_column();
        if (!hist_col_label.empty()) hist_column();
        if (duplicate_threshold > 0) remove_duplicate();

        std::cout << " Done!" << std::endl;
    }

    MetaDataTable read_check_ignore_optics(const FileName &fn, std::string tablename = "discover") {
        MetaDataTable MD;
        if (do_ignore_optics) {
            MD.read(fn, tablename);
        } else {
            ObservationModel::loadSafely(fn, obsModel, MD, tablename, 1, false);
            if (obsModel.opticsMdt.empty()) {
                std::cerr << " + WARNGING: could not read optics groups table, proceeding without it ..." << std::endl;
                MD.read(fn, tablename);
                do_ignore_optics = true;
            }
        }
        return MD;
    }

    void write_check_ignore_optics(MetaDataTable &MD, FileName fn, std::string tablename) {
        if (do_ignore_optics) MD.write(fn);
        else obsModel.save(MD, fn, tablename);
    }

    void compare() {

        // Read in the observationModel
        MetaDataTable MD2 = read_check_ignore_optics(fn_compare);
        // read_check_ignore_optics() overwrites the member variable obsModel (BAD DESIGN!)
        // so we have to back up.
        ObservationModel obsModelCompare = obsModel;
        MetaDataTable MD1 = read_check_ignore_optics(fn_in);

        EMDL::EMDLabel label1 = EMDL::str2Label(fn_label1);
        EMDL::EMDLabel label2 = fn_label2.empty() ? EMDL::UNDEFINED : EMDL::str2Label(fn_label2);
        EMDL::EMDLabel label3 = fn_label3.empty() ? EMDL::UNDEFINED : EMDL::str2Label(fn_label3);

        MetaDataTable MDonly1, MDonly2, MDboth;
        compareMetaDataTable(MD1, MD2, MDboth, MDonly1, MDonly2, label1, eps, label2, label3);

        std::cout << MDboth.size()  << " entries occur in both input STAR files."        << std::endl;
        std::cout << MDonly1.size() << " entries occur only in the 1st input STAR file." << std::endl;
        std::cout << MDonly2.size() << " entries occur only in the 2nd input STAR file." << std::endl;

        write_check_ignore_optics(MDboth, fn_out.insertBeforeExtension("_both"), MD1.name);
        std::cout << " Written: " << fn_out.insertBeforeExtension("_both") << std::endl;
        write_check_ignore_optics(MDonly1, fn_out.insertBeforeExtension("_only1"), MD1.name);
        std::cout << " Written: " << fn_out.insertBeforeExtension("_only1") << std::endl;
        // Use MD2's optics group for MDonly2.
        obsModel = obsModelCompare;
        write_check_ignore_optics(MDonly2, fn_out.insertBeforeExtension("_only2"), MD1.name);
        std::cout << " Written: " << fn_out.insertBeforeExtension("_only2") << std::endl;
    }

    void select() {

        MetaDataTable MDin = read_check_ignore_optics(fn_in);

        MetaDataTable MDout = subsetMetaDataTable(MDin, EMDL::str2Label(select_label), select_minval, select_maxval);

        write_check_ignore_optics(MDout, fn_out, MDin.name);
        std::cout << " Written: " << fn_out << " with " << MDout.size() << " item(s)" << std::endl;
    }

    void select_by_str() {
        int c = 0
              + !select_include_str.empty()
              + !select_exclude_str.empty();
        if (c != 1)
            REPORT_ERROR("You must specify only and at least one of --select_include and --select_exclude");

        MetaDataTable MDin = read_check_ignore_optics(fn_in);

        MetaDataTable MDout = !select_include_str.empty() ?
            subsetMetaDataTable(MDin, EMDL::str2Label(select_str_label), select_include_str, false) :
            subsetMetaDataTable(MDin, EMDL::str2Label(select_str_label), select_exclude_str, true);

        write_check_ignore_optics(MDout, fn_out, MDin.name);
        std::cout << " Written: " << fn_out << std::endl;

    }

    void discard_on_image_stats() {
        MetaDataTable MDin = read_check_ignore_optics(fn_in);

        std::cout << " Calculating average and stddev for all images ... " << std::endl;
        time_config();
        init_progress_bar(MDin.size());


        RFLOAT sum_avg = 0.0;
        RFLOAT sum2_avg = 0.0;
        RFLOAT sum_stddev = 0.0;
        RFLOAT sum2_stddev = 0.0;
        RFLOAT sum_n = 0.0;
        std::vector<RFLOAT> avgs, stddevs;
        long int ii = 0;
        for (long int i : MDin) {
            FileName fn_img = MDin.getValue<std::string>(EMDL::str2Label(discard_label), i);
            const auto img = Image<RFLOAT>::from_filename(fn_img);
            const auto stats = computeStats(img());
            sum_avg     += stats.avg;
            sum2_avg    += stats.avg * stats.avg;
            sum_stddev  += stats.stddev;
            sum2_stddev += stats.stddev * stats.stddev;
            sum_n += 1.0;
            avgs.push_back(stats.avg);
            stddevs.push_back(stats.stddev);

            ii++;
            if (ii % 100 == 0)
                progress_bar(ii);
        }

        progress_bar(MDin.size());

        sum_avg /= sum_n;
        sum_stddev /= sum_n;
        sum2_avg = sqrt(sum2_avg / sum_n - sum_avg * sum_avg);
        sum2_stddev = sqrt(sum2_stddev / sum_n - sum_stddev * sum_stddev);

        std::cout << " [ Average , stddev ] of the average Image value = [ " << sum_avg<< " , " << sum2_avg << " ] " << std::endl;
        std::cout << " [ Average , stddev ] of the stddev  Image value = [ " << sum_stddev<< " , " << sum2_stddev << " ] "  << std::endl;

        long int i = 0, nr_discard = 0;
        MetaDataTable MDout;
        for (long int index : MDin) {
            if (
                avgs[i] > sum_avg - discard_sigma * sum2_avg &&
                avgs[i] < sum_avg + discard_sigma * sum2_avg &&
                stddevs[i] > sum_stddev - discard_sigma * sum2_stddev &&
                stddevs[i] < sum_stddev + discard_sigma * sum2_stddev
            ) {
                MDout.addObject(MDin.getObject(index));
            } else {
                nr_discard++;
            }
            i++;
        }

        std::cout << " Discarded " << nr_discard << " Images because of too large or too small average/stddev values " << std::endl;

        write_check_ignore_optics(MDout, fn_out, MDin.name);
        std::cout << " Written: " << fn_out << std::endl;
    }

    void combine() {

        std::vector<FileName> fns_in;
        std::vector<std::string> words;
        tokenize(fn_in, words);
        for (int iword = 0; iword < words.size(); iword++) {
            FileName fnt = words[iword];
            fnt.globFiles(fns_in, false);
        }

        std::vector<MetaDataTable> MDsin, MDoptics;
        std::vector<ObservationModel> obsModels;
        // Read the first table into the global obsModel
        MetaDataTable MDin = read_check_ignore_optics(fns_in[0]);
        MDsin.push_back(MDin);
        // Read all the rest of the tables into local obsModels
        for (const FileName &fn : fns_in) {
            ObservationModel myobsModel;
            if (do_ignore_optics) MDin.read(fn, tablename_in);
            else ObservationModel::loadSafely(fn, myobsModel, MDin, "discover", 1);
            MDsin.push_back(MDin);
            obsModels.push_back(myobsModel);
        }

        // Combine optics groups with the same EMDL::IMAGE_OPTICS_GROUP_NAME, make new ones for those with a different name
        if (!do_ignore_optics) {
            std::vector<std::string> optics_group_uniq_names;

            // Initialise optics_group_uniq_names with the first table
            for (long int i : obsModel.opticsMdt) {
                const std::string name = obsModel.opticsMdt.getValue<std::string>(EMDL::IMAGE_OPTICS_GROUP_NAME, i);
                optics_group_uniq_names.push_back(name);
            }

            // Now check uniqueness of the other tables
            for (int MDs_id = 1; MDs_id < fns_in.size(); MDs_id++) {
                auto &om = obsModels[MDs_id - 1];

                std::vector<int> new_optics_groups;
                for (long int i : MDsin[MDs_id]) {
                    const int og = MDsin[MDs_id].getValue<int>(EMDL::IMAGE_OPTICS_GROUP, i);
                    new_optics_groups.push_back(og);
                }

                MetaDataTable unique_opticsMdt;
                unique_opticsMdt.addMissingLabels(om.opticsMdt);

                for (long int i : om.opticsMdt) {
                    const std::string myname          = om.opticsMdt.getValue<std::string>(EMDL::IMAGE_OPTICS_GROUP_NAME, i);
                    const int         my_optics_group = om.opticsMdt.getValue<int>(EMDL::IMAGE_OPTICS_GROUP, i);

                    const auto it = std::find(
                        optics_group_uniq_names.begin(), optics_group_uniq_names.end(),
                        myname
                    );
                    const int new_group = it + 1 - optics_group_uniq_names.begin();  // start counting groups at 1, not 0!

                    // If myname is unique
                    if (it == optics_group_uniq_names.end()) {
                        std::cout << " + Adding new optics_group with name: " << myname << std::endl;

                        optics_group_uniq_names.push_back(myname);
                        // Add the line to the global obsModel
                        om.opticsMdt.setValue(EMDL::IMAGE_OPTICS_GROUP, new_group, i);

                        const long int j = unique_opticsMdt.addObject();
                        unique_opticsMdt.setObject(om.opticsMdt.getObject(i), j);
                    } else {
                        std::cout << " + Joining optics_groups with the same name: " << myname << std::endl;
                        std::cerr << " + WARNING: if these are different data sets, you might want to rename optics groups instead of joining them!" << std::endl;
                        std::cerr << " + WARNING: if so, manually edit the rlnOpticsGroupName column in the optics_groups table of your input STAR files." << std::endl;
                    }

                    if (my_optics_group != new_group) {
                        std::cout << " + Renumbering group " << myname << " from " << my_optics_group << " to " << new_group << std::endl;
                    }

                    // Update the optics_group entry for all particles in the MDsin
                    for (long int j : MDsin[MDs_id]) {
                        int old_optics_group = MDsin[MDs_id].getValue<int>(EMDL::IMAGE_OPTICS_GROUP, j);
                        if (old_optics_group == my_optics_group)
                            new_optics_groups[j] = new_group;
                    }
                }

                om.opticsMdt = unique_opticsMdt;

                for (long int i : MDsin[MDs_id]) {
                    MDsin[MDs_id].setValue(EMDL::IMAGE_OPTICS_GROUP, new_optics_groups[i], i);

                    // Also rename the rlnGroupName to not have groups overlapping from different optics groups
                    try {
                        const std::string name
                            = "optics" + integerToString(new_optics_groups[i]) + "_"
                            + MDsin[MDs_id].getValue<std::string>(EMDL::MLMODEL_GROUP_NAME, i);
                        MDsin[MDs_id].setValue(EMDL::MLMODEL_GROUP_NAME, name, i);
                    } catch (const char* errmsg) {}
                }
            }

            // Make one vector for combination of the optics tables
            MDoptics.push_back(obsModel.opticsMdt);
            for (int i = 1; i < fns_in.size(); i++) {
                MDoptics.push_back(obsModels[i - 1].opticsMdt);
            }

            // Check if anisotropic magnification and/or beam_tilt are present in some optics groups, but not in others.
            // If so, initialise the others correctly
            bool has_beamtilt = false, has_not_beamtilt = false;
            bool has_anisomag = false, has_not_anisomag = false;
            bool has_odd_zernike = false, has_not_odd_zernike = false;
            bool has_even_zernike = false, has_not_even_zernike = false;
            bool has_ctf_premultiplied = false, has_not_ctf_premultiplied = false;
            for (int i = 0; i < fns_in.size(); i++) {
                const MetaDataTable &mdt_optics = MDoptics[i];
                if (
                    mdt_optics.containsLabel(EMDL::IMAGE_BEAMTILT_X) ||
                    mdt_optics.containsLabel(EMDL::IMAGE_BEAMTILT_Y)
                ) {
                    has_beamtilt = true;
                } else {
                    has_not_beamtilt = true;  // What?
                }
                if (
                    mdt_optics.containsLabel(EMDL::IMAGE_MAG_MATRIX_00) &&
                    mdt_optics.containsLabel(EMDL::IMAGE_MAG_MATRIX_01) &&
                    mdt_optics.containsLabel(EMDL::IMAGE_MAG_MATRIX_10) &&
                    mdt_optics.containsLabel(EMDL::IMAGE_MAG_MATRIX_11)
                ) {
                    has_anisomag = true;
                } else {
                    has_not_anisomag = true;  // What?
                }
                if (mdt_optics.containsLabel(EMDL::IMAGE_ODD_ZERNIKE_COEFFS)) {
                    has_odd_zernike = true;
                } else {
                    has_not_odd_zernike = true;  // What?
                }
                if (mdt_optics.containsLabel(EMDL::IMAGE_EVEN_ZERNIKE_COEFFS)) {
                    has_even_zernike = true;
                } else {
                    has_not_even_zernike = true;  // What?
                }
                if (mdt_optics.containsLabel(EMDL::OPTIMISER_DATA_ARE_CTF_PREMULTIPLIED)) {
                    has_ctf_premultiplied = true;
                } else {
                    has_not_ctf_premultiplied = true;  // What?
                }
            }
            #ifdef DEBUG
            printf("has_beamtilt = %d, has_not_beamtilt = %d, has_anisomag = %d, has_not_anisomag = %d, has_odd_zernike = %d, has_not_odd_zernike = %d, has_even_zernike = %d, has_not_even_zernike = %d, has_ctf_premultiplied = %d, has_not_ctf_premultiplied = %d\n", has_beamtilt, has_not_beamtilt, has_anisomag, has_not_anisomag, has_odd_zernike, has_not_odd_zernike, has_even_zernike, has_not_even_zernike, has_ctf_premultiplied, has_not_ctf_premultiplied);
            #endif

            for (int i = 0; i < fns_in.size(); i++) {
                MetaDataTable &mdt_optics = MDoptics[i];
                if (has_beamtilt && has_not_beamtilt) {
                    if (!mdt_optics.containsLabel(EMDL::IMAGE_BEAMTILT_X)) {
                        for (long int i : mdt_optics) {
                            mdt_optics.setValue(EMDL::IMAGE_BEAMTILT_X, 0.0, i);
                        }
                    }
                    if (!mdt_optics.containsLabel(EMDL::IMAGE_BEAMTILT_Y)) {
                        for (long int i : mdt_optics) {
                            mdt_optics.setValue(EMDL::IMAGE_BEAMTILT_Y, 0.0, i);
                        }
                    }
                }

                if (has_anisomag && has_not_anisomag) {
                    if (
                        !mdt_optics.containsLabel(EMDL::IMAGE_MAG_MATRIX_00) ||
                        !mdt_optics.containsLabel(EMDL::IMAGE_MAG_MATRIX_01) ||
                        !mdt_optics.containsLabel(EMDL::IMAGE_MAG_MATRIX_10) ||
                        !mdt_optics.containsLabel(EMDL::IMAGE_MAG_MATRIX_11)
                    ) {
                        for (long int i : mdt_optics) {
                            // 2×2 identity matrix
                            mdt_optics.setValue(EMDL::IMAGE_MAG_MATRIX_00, 1.0, i);
                            mdt_optics.setValue(EMDL::IMAGE_MAG_MATRIX_01, 0.0, i);
                            mdt_optics.setValue(EMDL::IMAGE_MAG_MATRIX_10, 0.0, i);
                            mdt_optics.setValue(EMDL::IMAGE_MAG_MATRIX_11, 1.0, i);
                        }
                    }
                }

                if (has_odd_zernike && has_not_odd_zernike) {
                    std::vector<RFLOAT> six_zeros(6, 0);
                    if (!mdt_optics.containsLabel(EMDL::IMAGE_ODD_ZERNIKE_COEFFS)) {
                        for (long int i : mdt_optics) {
                            mdt_optics.setValue(EMDL::IMAGE_ODD_ZERNIKE_COEFFS, six_zeros, i);
                        }
                    }
                }

                if (has_even_zernike && has_not_even_zernike) {
                    std::vector<RFLOAT> nine_zeros(9, 0);
                    if (!mdt_optics.containsLabel(EMDL::IMAGE_EVEN_ZERNIKE_COEFFS)) {
                        for (long int i : mdt_optics) {
                            mdt_optics.setValue(EMDL::IMAGE_EVEN_ZERNIKE_COEFFS, nine_zeros, i);
                        }
                    }
                }

                if (has_ctf_premultiplied && has_not_ctf_premultiplied) {
                    if (!mdt_optics.containsLabel(EMDL::OPTIMISER_DATA_ARE_CTF_PREMULTIPLIED)) {
                        for (long int i : mdt_optics) {
                            mdt_optics.setValue(EMDL::OPTIMISER_DATA_ARE_CTF_PREMULTIPLIED, false, i);
                        }
                    }
                }
            }

            // Now combine all optics tables into one
            obsModel.opticsMdt = MetaDataTable::combineMetaDataTables(MDoptics);
        }

        // Combine the particles tables
        MetaDataTable MDout = MetaDataTable::combineMetaDataTables(MDsin);

        // Deactivate the group_name column
        MDout.deactivateLabel(EMDL::MLMODEL_GROUP_NO);

        if (!fn_check.empty()) {
            EMDL::EMDLabel label = EMDL::str2Label(fn_check);
            if (!MDout.containsLabel(label))
                REPORT_ERROR("ERROR: the output file does not contain the label to check for duplicates. Is it present in all input files?");

            // Don't want to mess up original order, so make a MDsort with only that label...
            FileName fn_prev = "";
            MetaDataTable MDsort;
            MDsort.reserve(MDout.size());
            for (long int i : MDout) {
                const FileName fn_this = MDout.getValue<std::string>(label, i);
                MDsort.addObject();
                MDsort.setValue(label, fn_this, i);
            }

            // Sort on the label
            if (EMDL::is<int>(label)) {
                MDsort.newSort<MD::CompareIntsAt>(label);
            } else if (EMDL::is<double>(label)) {
                MDsort.newSort<MD::CompareDoublesAt>(label);
            } else if (EMDL::is<std::string>(label)) {
                MDsort.newSort<MD::CompareStringsAt>(label);
            } else {
                REPORT_ERROR("Cannot sort this label: " + EMDL::label2Str(label));
            }

            long int nr_duplicates = 0;
            for (long int i : MDsort) {
                const FileName fn_this = MDsort.getValue<std::string>(label, i);
                if (fn_this == fn_prev) {
                    nr_duplicates++;
                    std::cerr << " WARNING: duplicate entry: " << fn_this << std::endl;
                }
                fn_prev = fn_this;
            }

            if (nr_duplicates > 0)
                std::cerr << " WARNING: Total number of duplicate "<< fn_check << " entries: " << nr_duplicates << std::endl;
        }

        write_check_ignore_optics(MDout, fn_out, MDin.name);
        std::cout << " Written: " << fn_out << std::endl;
    }

    void split() {
        MetaDataTable MD = read_check_ignore_optics(fn_in);

        // Randomise if neccesary
        if (do_random_order) {
            if (random_seed < 0) {
                randomize_random_generator();
            } else {
                init_random_generator(random_seed);
            }

            MD.randomiseOrder();
        }

        long int n_obj = MD.size();
        if (n_obj == 0) {
            REPORT_ERROR("ERROR: empty STAR file...");
        }

        if (nr_split < 0 && size_split < 0) {
            REPORT_ERROR("ERROR: nr_split and size_split are both zero. Set at least one of them to be positive.");
        } else if (nr_split < 0 && size_split > 0) {
            nr_split = ceil((double) n_obj / size_split);
        } else if (nr_split > 0 && size_split < 0) {
            size_split = ceil((double) n_obj / nr_split);
        }

        std::vector<MetaDataTable> MDouts (nr_split);

        long int n = 0;
        for (long int index : MD) {
            int my_split = n / size_split;
            if (my_split < nr_split) {
                MDouts[my_split].addObject(MD.getObject(index));
            } else {
                break;
            }
            n++;
        }

        // Sjors 19 Jun 2019: write out a star file with the output nodes
        MetaDataTable MDnodes;
        MDnodes.name = "output_nodes";
        for (int isplit = 0; isplit < nr_split; isplit ++) {
            const FileName fnt = fn_out.insertBeforeExtension("_split" + integerToString(isplit + 1));
            write_check_ignore_optics(MDouts[isplit], fnt, MD.name);
            std::cout << " Written: " << fnt << " with " << MDouts[isplit].size() << " objects." << std::endl;

            MDnodes.addObject();
            MDnodes.setValue(EMDL::PIPELINE_NODE_NAME, fnt, isplit);
            const int type =
                MD.name == "micrographs" ? Node::MICS :
                MD.name == "movies"      ? Node::MOVIES :
                                           Node::PART_DATA;  // Otherwise, assume these are particles.

            MDnodes.setValue(EMDL::PIPELINE_NODE_TYPE, type, isplit);
        }

        // Write out the star file with the output nodes
        FileName mydir = fn_out.beforeLastOf("/");
        if (mydir.empty()) { mydir = "."; }
        MDnodes.write(mydir + "/" + RELION_OUTPUT_NODES);

    }

    void operate() {
        EMDL::EMDLabel label1, label2, label3;
        label1 = EMDL::str2Label(fn_operate);
        if (!fn_operate2.empty())
        label2 = EMDL::str2Label(fn_operate2);
        if (!fn_operate3.empty())
        label3 = EMDL::str2Label(fn_operate3);

        MetaDataTable MD = read_check_ignore_optics(fn_in);

        for (long int i : MD) {
            if (EMDL::is<double>(label1)) {
                RFLOAT val;
                if (!fn_set.empty()) {
                    val = textToFloat(fn_set);
                    MD.setValue(label1, val, i);
                    if (!fn_operate2.empty()) MD.setValue(label2, val, i);
                    if (!fn_operate3.empty()) MD.setValue(label3, val, i);
                } else if (multiply_by != 1.0 || add_to != 0.0) {
                    val = MD.getValue<RFLOAT>(label1, i);
                    val = multiply_by * val + add_to;
                    MD.setValue(label1, val, i);
                    if (!fn_operate2.empty()) {
                        val = MD.getValue<RFLOAT>(label2, i);
                        val = multiply_by * val + add_to;
                        MD.setValue(label2, val, i);
                    }
                    if (!fn_operate3.empty()) {
                        val = MD.getValue<RFLOAT>(label3, i);
                        val = multiply_by * val + add_to;
                        MD.setValue(label3, val, i);
                    }
                }
            } else if (EMDL::is<int>(label1)) {
                int val;
                if (!fn_set.empty()) {
                    val = textToInteger(fn_set);
                    MD.setValue(label1, val, i);
                    if (!fn_operate2.empty()) MD.setValue(label2, val, i);
                    if (!fn_operate3.empty()) MD.setValue(label3, val, i);
                } else if (multiply_by != 1.0 || add_to != 0.0) {
                    val = MD.getValue<int>(label1, i);
                    val = multiply_by * val + add_to;
                    MD.setValue(label1, val, i);
                    if (!fn_operate2.empty()) {
                        val = MD.getValue<int>(label2, i);
                        val = multiply_by * val + add_to;
                        MD.setValue(label2, val, i);
                    }
                    if (!fn_operate3.empty()) {
                        val = MD.getValue<int>(label3, i);
                        val = multiply_by * val + add_to;
                        MD.setValue(label3, val, i);
                    }
                }
            } else if (EMDL::is<std::string>(label1)) {
                if (!fn_set.empty()) {
                    MD.setValue(label1, fn_set, i);
                    if (!fn_operate2.empty()) MD.setValue(label2, fn_set, i);
                    if (!fn_operate3.empty()) MD.setValue(label3, fn_set, i);
                } else if (multiply_by != 1.0 || add_to != 0.0) {
                    REPORT_ERROR("ERROR: cannot multiply_by or add_to a string!");
                }
            } else if (EMDL::is<bool>(label1)) {
                REPORT_ERROR("ERROR: cannot operate on a boolean!");
            } else if (EMDL::is<bool>(label1)) {  // What?
                // @TODO:
                REPORT_ERROR("ERROR: cannot operate on vectors (yet)!");
            }
        }

        write_check_ignore_optics(MD, fn_out, MD.name);
        std::cout << " Written: " << fn_out << std::endl;
    }

    void center() {
        MetaDataTable MD = read_check_ignore_optics(fn_in, "particles");
        bool do_contains_xy = MD.containsLabel(EMDL::ORIENT_ORIGIN_X_ANGSTROM) && MD.containsLabel(EMDL::ORIENT_ORIGIN_Y_ANGSTROM);
        bool do_contains_z = MD.containsLabel(EMDL::ORIENT_ORIGIN_Z_ANGSTROM);

        if (!do_contains_xy) {
            REPORT_ERROR("ERROR: input STAR file does not contain rlnOriginX/Y for re-centering.");
        }

        Vector<RFLOAT> my_center {center_X, center_Y, center_Z};

        for (long int i: MD) {

            RFLOAT angpix;
            if (do_ignore_optics) {
                angpix = cl_angpix;
            } else {
                const int optics_group = MD.getValue<int>(EMDL::IMAGE_OPTICS_GROUP, i) - 1;
                angpix = obsModel.getPixelSize(optics_group);
            }

            RFLOAT xoff = MD.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_X_ANGSTROM, i) / angpix;
            RFLOAT yoff = MD.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_Y_ANGSTROM, i) / angpix;
            RFLOAT rot  = MD.getValue<RFLOAT>(EMDL::ORIENT_ROT,  i);
            RFLOAT tilt = MD.getValue<RFLOAT>(EMDL::ORIENT_TILT, i);
            RFLOAT psi  = MD.getValue<RFLOAT>(EMDL::ORIENT_PSI,  i);

            // Project the center-coordinates
            Matrix<RFLOAT> A3D = Euler::angles2matrix(rot, tilt, psi);
            Vector<RFLOAT> my_projected_center = matmul(A3D, my_center);

            xoff -= my_projected_center[0];
            yoff -= my_projected_center[1];

            // Set back the new centers
            MD.setValue(EMDL::ORIENT_ORIGIN_X_ANGSTROM, xoff * angpix, i);
            MD.setValue(EMDL::ORIENT_ORIGIN_Y_ANGSTROM, yoff * angpix, i);

            // also allow 3D data (subtomograms)
            RFLOAT zoff;
            if (do_contains_z) {
                zoff = MD.getValue<RFLOAT>(EMDL::ORIENT_ORIGIN_Z_ANGSTROM, i) / angpix;
                zoff -= my_projected_center[2];
                MD.setValue(EMDL::ORIENT_ORIGIN_Z_ANGSTROM, zoff * angpix, i);
            }
        }

        write_check_ignore_optics(MD, fn_out, MD.name);
        std::cout << " Written: " << fn_out << std::endl;
    }

    void remove_column() {
        MetaDataTable MD = read_check_ignore_optics(fn_in);
        MD.deactivateLabel(EMDL::str2Label(remove_col_label));
        write_check_ignore_optics(MD, fn_out, MD.name);
        std::cout << " Written: " << fn_out << std::endl;
    }

    void add_column() {
        if (add_col_value.empty() == add_col_from.empty()) {
            REPORT_ERROR("ERROR: you need to specify either --add_column_value or --copy_column_from when adding a column.");
        }

        bool set_value = !add_col_value.empty();

        EMDL::EMDLabel label = EMDL::str2Label(add_col_label);
        EMDL::EMDLabel source_label;

        MetaDataTable MD = read_check_ignore_optics(fn_in);
        MD.addLabel(label);

        if (!add_col_from.empty()) {
            source_label = EMDL::str2Label(add_col_from);
            if (!MD.containsLabel(source_label)) {
                REPORT_ERROR("ERROR: The column specified in --add_column_from is not present in the input STAR file.");
            }
        }

        for (long int i : MD) {
            if (EMDL::is<double>(label)) {
                RFLOAT aux = set_value ? textToFloat(add_col_value) : MD.getValue<RFLOAT>(source_label, i);
                MD.setValue(label, aux, i);
            } else if (EMDL::is<int>(label)) {
                long aux = set_value ? textToInteger(add_col_value) : MD.getValue<long int>(source_label, i);
                MD.setValue(label, aux, i);
            } else if (EMDL::is<bool>(label)) {
                bool aux = set_value ? textToInteger(add_col_value) : MD.getValue<bool>(source_label, i);
                MD.setValue(label, aux, i);
            } else if (EMDL::is<std::string>(label)) {
                std::string aux = set_value ? add_col_value : MD.getValue<std::string>(source_label, i);
                MD.setValue(label, add_col_value, i);
            } else if (EMDL::is<std::string>(label)) {  // What?
                std::string auxStr = set_value ? add_col_value : MD.getValueToString(source_label, i);
                MD.setValueFromString(label, add_col_value, i);
            }
        }

        write_check_ignore_optics(MD, fn_out, MD.name);
        std::cout << " Written: " << fn_out << std::endl;
    }

    void hist_column() {
        EMDL::EMDLabel label = EMDL::str2Label(hist_col_label);

        MetaDataTable MD = read_check_ignore_optics(fn_in);
        if (!MD.containsLabel(label))
            REPORT_ERROR("ERROR: The column specified in --hist_column is not present in the input STAR file.");

        std::vector<RFLOAT> histX, histY;
        CPlot2D *plot2D = new CPlot2D("");
        PlotMetaData::columnHistogram(MD, label, histY, histX, 1, plot2D, nr_bin, hist_min, hist_max, show_frac, show_cumulative);
        FileName fn_eps = fn_out.withoutExtension()+".eps";
        plot2D->OutputPostScriptPlot(fn_eps);
        std::cout << " Done! written out " << fn_eps << std::endl;
        delete plot2D;

    }

    void remove_duplicate() {
        if (do_ignore_optics)
            REPORT_ERROR("Duplicate removal is not compatible with --ignore_optics");

        MetaDataTable MD = read_check_ignore_optics(fn_in, "particles");

        EMDL::EMDLabel mic_label;
        if (MD.containsLabel(EMDL::MICROGRAPH_NAME)) {
            mic_label = EMDL::MICROGRAPH_NAME;
        } else {
            REPORT_ERROR("The input STAR file does not contain rlnMicrographName column.");
        }

        RFLOAT particle_angpix = 1.0; // rlnOriginX/YAngst is always 1 A/px.

        if (obsModel.numberOfOpticsGroups() > 1)
            std::cerr << "WARNING: The input contains multiple optics groups. We assume that the pixel sizes of original micrographs before extraction are all the same. If this is not the case, you have to split the input and remove duplicates separately." << std::endl;

        if (extract_angpix > 0) {
            std::cout << " + Using the provided pixel size for original micrographs before extraction: " << extract_angpix << std::endl;
        } else {
            extract_angpix = obsModel.getPixelSize(0);
            std::cout << " + Assuming the pixel size of original micrographs before extraction is " << extract_angpix << std::endl;
        }

        RFLOAT scale = particle_angpix / extract_angpix;
        RFLOAT duplicate_threshold_in_px = duplicate_threshold / extract_angpix;

        std::cout << " + The minimum inter-particle distance " << duplicate_threshold << " A corresponds to " << duplicate_threshold_in_px << " px in the micrograph coordinate (rlnCoordinateX/Y)." << std::endl;
        std::cout << " + The particle shifts (rlnOriginXAngst, rlnOriginYAngst) are multiplied by " << scale << " to bring it to the same scale as rlnCoordinateX/Y." << std::endl;
        FileName fn_removed = fn_out.withoutExtension() + "_removed.star";

        MetaDataTable MDout = removeDuplicatedParticles(MD, mic_label, duplicate_threshold_in_px, scale, fn_removed, true);

        write_check_ignore_optics(MDout, fn_out, "particles");
        std::cout << " Written: " << fn_out << std::endl;
    }
};

int main(int argc, char *argv[]) {
    star_handler_parameters prm;
    try {
        prm.read(argc, argv);
        prm.run();
    } catch (RelionError XE) {
        std::cerr << XE;
        // prm.usage();
        return RELION_EXIT_FAILURE;
    }
    return RELION_EXIT_SUCCESS;
}
