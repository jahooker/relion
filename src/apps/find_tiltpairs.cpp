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
#include <src/strings.h>
#include <src/funcs.h>
#include <src/memory.h>
#include <src/euler.h>
#include <src/time.h>
#include <src/metadata_table.h>

class angular_error_parameters {

    public:

    FileName fn_unt, fn_til, fn_out;
    MetaDataTable MDunt, MDtil;
    RFLOAT tilt, tilt0, tiltF, tiltStep;
    RFLOAT rot, rot0, rotF, rotStep;
    int size, dim;
    int x0, xF, xStep;
    int y0, yF, yStep;
    RFLOAT acc;
    int mind2;
    bool do_opt;
    RFLOAT best_rot, best_tilt;
    int best_x, best_y;
    Matrix<RFLOAT> Pass;
    std::vector<int> p_unt, p_til, p_map, pairs_t2u;
    // I/O Parser
    IOParser parser;

    void usage() {
        parser.writeUsage(std::cerr);
    }

    void read(int argc, char **argv) {
        parser.setCommandLine(argc, argv);

        int general_section = parser.addSection("General Options");
        fn_unt = parser.getOption("--u", "STAR file with the untilted xy-coordinates");
        fn_til = parser.getOption("--t", "STAR file with the untilted xy-coordinates");
        size = textToInteger(parser.getOption("--size", "Largest dimension of the micrograph (in pixels), e.g. 4096"));
        dim = textToInteger(parser.getOption("--dim", "Dimension of boxed particles (for EMAN .box files in pixels)", "200"));
        acc = textToFloat(parser.getOption("--acc", "Allowed accuracy (in pixels), e.g. half the particle diameter"));
        tilt = textToFloat(parser.getOption("--tilt", "Fix tilt angle (in degrees)", "99999.0"));
        rot = textToFloat(parser.getOption("--rot", "Fix direction of the tilt axis (in degrees), 0 = along y, 90 = along x", "99999.0"));
        do_opt = !parser.checkOption("--dont_opt", "Skip optimization of the transformation matrix");
        mind2 = round(acc * acc);

        int angle_section = parser.addSection("Specified tilt axis and translational search ranges");
        tilt0 = textToFloat(parser.getOption("--tilt0", "Minimum tilt angle (in degrees)","0.0"));
        tiltF = textToFloat(parser.getOption("--tiltF", "Maximum tilt angle (in degrees)","99999.0"));
        if (tiltF == 99999.0) { tiltF = tilt0; }
        tiltStep = textToFloat(parser.getOption("--tiltStep", "Tilt angle step size (in degrees)","1.0"));

        rot0 = textToFloat(parser.getOption("--rot0", "Minimum rot angle (in degrees)","0.0"));
        rotF = textToFloat(parser.getOption("--rotF", "Maximum rot angle (in degrees)","99999.0"));
        if (rotF == 99999.0) { rotF = rot0; }
        rotStep = textToFloat(parser.getOption("--rotStep", "Rot angle step size (in degrees)","1.0"));

        x0 = textToInteger(parser.getOption("--x0", "Minimum X offset (pixels)","-99999"));
        xF = textToInteger(parser.getOption("--xF", "Maximum X offset (pixels)","99999"));
        xStep = textToInteger(parser.getOption("--xStep", "X offset step size (pixels)","-1"));
        y0 = textToInteger(parser.getOption("--y0", "Minimum Y offset (pixels)","-99999"));
        yF = textToInteger(parser.getOption("--yF", "Maximum Y offset (pixels)","99999"));
        yStep = textToInteger(parser.getOption("--yStep", "Y offset step size (pixels)","-1"));

        // Check for errors in the command-line option
        if (parser.checkForErrors())
            REPORT_ERROR("Errors encountered on the command line, exiting...");

        // If tilt and rot were given: do not search those
        if (tilt != 99999.0) {
            tilt0 = tiltF = tilt;
            tiltStep = 1.0;
        }
        if (rot != 99999.0) {
            rot0 = rotF = rot;
            rotStep = 1.0;
        }

        // By default search the entire micrograph
        x0 = std::max(x0, -size);
        xF = std::min(xF,  size);
        // By default use a xStep of one third the accuracy
        if (xStep < 0) { xStep = acc / 3; }

        // By default treat y search in the same way as the x-search
        if (y0 == -99999) { y0 = x0; }
        if (yF == +99999) { yF = xF; }
        if (yStep < 0) { yStep = xStep; }

        // Done reading, now fill p_unt and p_til
        MDunt.read(fn_unt);
        MDtil.read(fn_til);

        // Check for the correct labels
        if (!MDunt.containsLabel(EMDL::IMAGE_COORD_X) || !MDunt.containsLabel(EMDL::IMAGE_COORD_Y))
            REPORT_ERROR("ERROR: Untilted STAR file does not contain the rlnCoordinateX or Y labels");
        if (!MDtil.containsLabel(EMDL::IMAGE_COORD_X) || !MDtil.containsLabel(EMDL::IMAGE_COORD_Y))
            REPORT_ERROR("ERROR: Tilted STAR file does not contain the rlnCoordinateX or Y labels");

        p_unt.clear();
        for (long int i : MDunt) {
            p_unt.push_back((int) MDunt.getValue<RFLOAT>(EMDL::IMAGE_COORD_X, i));
            p_unt.push_back((int) MDunt.getValue<RFLOAT>(EMDL::IMAGE_COORD_Y, i));
        }

        p_til.clear();
        for (long int i : MDtil) {
            p_til.push_back((int) MDtil.getValue<RFLOAT>(EMDL::IMAGE_COORD_X, i));
            p_til.push_back((int) MDtil.getValue<RFLOAT>(EMDL::IMAGE_COORD_Y, i));
        }

        // Initialize best transformation params
        best_x = best_y = 9999;
        best_rot = best_tilt = 9999.0;
    }

    int getNumberOfPairs(int dx=0, int dy=0) {
        pairs_t2u.clear();
        pairs_t2u.resize(p_til.size() / 2, -1);
        int npairs = 0;
        for (int u = 0; u < p_map.size() / 2; u++)
        for (int t = 0; t < p_til.size() / 2; t++) {
            // only search over particles that do not have a pair yet
            if (pairs_t2u[t] < 0) {
                int X = p_map[2 * u    ] - p_til[2 * t    ] + dx;
                int Y = p_map[2 * u + 1] - p_til[2 * t + 1] + dy;
                if (X * X + Y * Y < mind2) {
                    npairs++;
                    pairs_t2u[t] = u;
                    // No longer have to search for all the others in q
                    break;
                }
            }
        }
        return npairs;
    }

    RFLOAT getAverageDistance(int dx=0, int dy=0) {
        FileName fn_map = "dist.txt";
        std::ofstream fh;
        fh.open(fn_map.c_str(), std::ios::out);

        RFLOAT sum_of_distances = 0.0;
        int count = 0;
        for (int t = 0; t < pairs_t2u.size(); t++) {
            int u = pairs_t2u[t];
            if (u >= 0) {
                int X = p_map[2 * u    ] - p_til[2 * t    ] + dx;
                int Y = p_map[2 * u + 1] - p_til[2 * t + 1] + dy;
                int XXYY = X * X + Y * Y;
                // std::cerr << " sqrt(XXYY)= " << sqrt(XXYY) << " t= " << t << " u= " << u << std::endl;
                sum_of_distances += sqrt(XXYY);
                fh << sqrt(XXYY) << std::endl;
                count++;
            }
        }
        fh.close();
        return sum_of_distances / (RFLOAT) count;
    }

    int prunePairs(int dx=0, int dy=0) {
        int nprune = 0;
        // Prune for RFLOAT pairs
        for (int t = 0; t < pairs_t2u.size(); t++) {
            int u = pairs_t2u[t];
            if (u >= 0) {
                for (int tp = t + 1; tp < pairs_t2u.size(); tp++) {
                    int up = pairs_t2u[tp];
                    // Find pairs to the same tilted position
                    if (up == u) {
                        nprune++;

                        // Only keep the nearest neighbours as a pair
                        int X = p_map[2 * u    ] - p_til[2 * t    ] + dx;
                        int Y = p_map[2 * u + 1] - p_til[2 * t + 1] + dy;

                        // up == p
                        int Xp = p_map[2 * u]     - p_til[2 * tp    ] + dx;
                        int Yp = p_map[2 * u + 1] - p_til[2 * tp + 1] + dy;

                        if (X * X + Y * Y < Xp * Xp + Yp * Yp) {
                            pairs_t2u[tp] = -1;
                        } else {
                            pairs_t2u[t]  = -1;
                        }
                    }
                }
            }
        }
        return nprune;
    }

    void mapOntoTilt() {
        p_map.resize(p_unt.size());
        for (int u = 0; u < p_map.size() / 2; u++) {
            RFLOAT xu = p_unt[2 * u];
            RFLOAT yu = p_unt[2 * u + 1];

            p_map[2 * u    ] = round(Pass.at(0, 0) * xu + Pass.at(0, 1) * yu + Pass.at(0, 2));
            p_map[2 * u + 1] = round(Pass.at(1, 0) * xu + Pass.at(1, 1) * yu + Pass.at(1, 2));

        }
    }

    RFLOAT optimiseTransformationMatrix(bool do_optimise_nr_pairs) {
        std::vector<int> best_pairs_t2u, best_map;
        RFLOAT score, best_dist = 9999.0;
        RFLOAT best_score = do_optimise_nr_pairs ? 0.0 : -999999.0;

        int nn = std::max(1, (int) ((rotF  - rot0)  / rotStep))
               * std::max(1, (int) ((tiltF - tilt0) / tiltStep))
               * std::max(1, (int) ((xF    - x0)    / xStep))
               * std::max(1, (int) ((yF    - y0)    / yStep));
        int n = 0;
        init_progress_bar(nn);
        for (RFLOAT rot = rot0; rot <= rotF; rot += rotStep)
        for (RFLOAT tilt = tilt0; tilt <= tiltF; tilt += tiltStep) {
            // Assume tilt-axis lies in-plane...
            RFLOAT psi = -rot;
            // Rotate all points correspondingly
            Pass = Euler::angles2matrix(rot, tilt, psi);
            //std::cerr << " Pass= " << Pass << std::endl;
            // Zero-translations for now (these are added in the x-y loops below)
            Pass.at(0, 2) = Pass.at(1, 2) = 0.0;
            mapOntoTilt();
            for (int x = x0; x <= xF; x += xStep)
            for (int y = y0; y <= yF; y += yStep, n++) {

                score = do_optimise_nr_pairs ? 
                    getNumberOfPairs(x, y) : -getAverageDistance(x, y); // negative because smaller distance is better!
                /** NOTE: 
                 * There is more than one way of rewarding small values.
                 * We could, for example, take the reciprocal.
                 * But negation is faster than division.
                 * In general, we seek an appropriate monotonic decreasing function.
                */

                bool is_best = false;
                if (do_optimise_nr_pairs && score == best_score) {
                    RFLOAT dist = getAverageDistance(x, y);
                    if (dist < best_dist) {
                        best_dist = dist;
                        is_best = true;
                    }
                }
                if (score > best_score || is_best) {
                    best_score = score;
                    best_pairs_t2u = pairs_t2u;
                    best_rot = rot;
                    best_tilt = tilt;
                    best_x = x;
                    best_y = y;
                }
                if (n % 1000 == 0) progress_bar(n);
            }
        }
        progress_bar(nn);
        // Update pairs with the best_pairs
        if (do_optimise_nr_pairs)
            pairs_t2u = best_pairs_t2u;

        // Update the Passing matrix and the mapping
        Pass = Euler::angles2matrix(best_rot, best_tilt, -best_rot);
        // Zero-translations for now (these are added in the x-y loops below)
        Pass.at(0, 2) = Pass.at(1, 2) = 0.0;
        mapOntoTilt();
        return best_score;
    }

    void optimiseTransformationMatrixContinuous() {
        // Get coordinates of all pairs:
        Matrix<RFLOAT> Au = Matrix<RFLOAT>::zeros(3, 3);
        Matrix<RFLOAT> Bt = Matrix<RFLOAT>::zeros(3, 3);
        Pass.resize(4, 4);
        std::fill(Pass.begin(), Pass.end(), 0);

        // Add all pairs to dependent matrices (adapted from add_point in Xmipps micrograph_mark main_widget_mark.cpp)
        for (int t = 0; t < pairs_t2u.size(); t++) {
            int u = pairs_t2u[t];
            if (u >= 0) {
                Au(0, 0) += (RFLOAT) (p_unt[2 * u] * p_unt[2 * u    ]);
                Au(0, 1) += (RFLOAT) (p_unt[2 * u] * p_unt[2 * u + 1]);
                Au(0, 2) += (RFLOAT) (p_unt[2 * u]);
                Au(1, 0) = Au(0, 1);
                Au(1, 1) += (RFLOAT) (p_unt[2 * u + 1] * p_unt[2 * u + 1]);
                Au(1, 2) += (RFLOAT) (p_unt[2 * u + 1]);
                Au(2, 0) = Au(0, 2);
                Au(2, 1) = Au(1, 2);
                Au(2, 2) += 1.0;

                Bt(0, 0) += (RFLOAT) (p_til[2 * t    ] * p_unt[2 * u]);
                Bt(0, 1) += (RFLOAT) (p_til[2 * t + 1] * p_unt[2 * u]);
                Bt(0, 2) = Au(0, 2);
                Bt(1, 0) += (RFLOAT) (p_til[2 * t    ] * p_unt[2 * u + 1]);
                Bt(1, 1) += (RFLOAT) (p_til[2 * t + 1] * p_unt[2 * u + 1]);
                Bt(1, 2) = Au(1, 2);
                Bt(2, 0) += (RFLOAT) (p_til[2 * t    ]);
                Bt(2, 1) += (RFLOAT) (p_til[2 * t + 1]);
                Bt(2, 2) += 1.0;
            }
        }

        // Solve equations
        solve(Au, Bt, Pass);
        Pass = Pass.transpose();
        std::cout << " Optimised passing matrix= " << Pass << std::endl;
        // These values can be complete CRAP. Better not show them at all....
        // RFLOAT rotp, tiltp, psip;
        // tiltp = acos(Pass(1, 1));
        // rotp  = acos(Pass(1, 0) /  sin(tiltp));
        // psip  = acos(Pass(0, 1) / -sin(tiltp));
        // std::cout << " Optimised tilt angle= " << degrees(tiltp) << std::endl;
        // std::cout << " Optimised in-plane rot angles= " << degrees(rotp) <<" and "<< degrees(psip) << std::endl;
        // Map using the new matrix
        mapOntoTilt();

    }

    void run() {

        // First do a crude search over the given parameter optimization space
        // Optimize the number of pairs here...
        int npart = optimiseTransformationMatrix(true);
        // Get rid of RFLOAT pairs (two different untilted coordinates are close to a tilted coordinate)
        int nprune = 0;
        nprune = prunePairs(best_x, best_y);
        // Calculate average distance between the pairs
        RFLOAT avgdist = getAverageDistance(best_x, best_y);
        std::cout << " Before optimization of the passing matrix: "<<std::endl;
        std::cout << "  - Number of pruned pairs= "<<nprune<<std::endl;
        std::cout << "  - best_rot= " << best_rot << " best_tilt= " << best_tilt << " best_x= " << best_x << " best_y= " << best_y << std::endl;
        std::cout << "  - Number of particle pairs= " << npart << " average distance= " <<avgdist<<std::endl;

        #define WRITE_MAPPED
        #ifdef WRITE_MAPPED
        FileName fn_map = "mapped.box";
        std::ofstream fh;
        fh.open(fn_map.c_str(), std::ios::out);
        for (int i = 0; i < p_map.size() / 2; i++) {
            fh << p_map[2 * i] + best_x -dim / 2 << " " << p_map[2 * i + 1] + best_y -dim / 2 << " " << dim << " " << dim << " -3" << std::endl;
            // if (pairs[i]>=0)
            //     std::cerr << " i= " << i << " pairs[i]= " << pairs[i] << std::endl;
        }
        fh.close();
        #endif

        if (do_opt) {
            optimiseTransformationMatrixContinuous();
            npart = getNumberOfPairs();
            nprune = prunePairs();
            avgdist = getAverageDistance();
            std::cout << " After optimization of the passing matrix: " << std::endl;
            std::cout << "  - Number of pruned pairs= " << nprune << std::endl;
            std::cout << "  - Final number of particle pairs= " << npart << " average distance= " << avgdist << std::endl;

        }

        #ifdef WRITE_MAPPED
        fn_map = "mapped_opt.box";
        fh.open(fn_map.c_str(), std::ios::out);
        for (int i = 0; i < p_map.size() / 2; i++) {
            fh << p_map[2 * i] -dim / 2 << " " << p_map[2 * i + 1] -dim / 2 << " " << dim << " " << dim << " -3" << std::endl;
        }
        fh.close();
        #endif

        // Write out STAR files with the coordinates
        MetaDataTable MDu, MDt;
        for (int t = 0; t < p_til.size() / 2; t++) {
            int u = pairs_t2u[t];
            if (u >= 0) {
                MDu.addObject();
                MDu.setValue(EMDL::IMAGE_COORD_X, (RFLOAT) p_unt[2 * u    ], t);
                MDu.setValue(EMDL::IMAGE_COORD_Y, (RFLOAT) p_unt[2 * u + 1], t);

                MDt.addObject();
                MDt.setValue(EMDL::IMAGE_COORD_X, (RFLOAT) p_til[2 * t    ], t);
                MDt.setValue(EMDL::IMAGE_COORD_Y, (RFLOAT) p_til[2 * t + 1], t);
            }
        }
        fn_unt = fn_unt.withoutExtension() + "_pairs.star";
        fn_til = fn_til.withoutExtension() + "_pairs.star";
        MDu.write(fn_unt);
        MDt.write(fn_til);

        std::cout << " Written out coordinate STAR files: " << fn_unt << " and " << fn_til <<std::endl;
        std::cout << " Done!" << std::endl;

    }

};

int main(int argc, char *argv[]) {
    angular_error_parameters prm;

    try {
        prm.read(argc, argv);
        prm.run();
    } catch (RelionError XE) {
        std::cerr << XE;
        return RELION_EXIT_FAILURE;
    }

    return RELION_EXIT_SUCCESS;
}
