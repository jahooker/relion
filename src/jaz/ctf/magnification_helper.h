/***************************************************************************
 *
 * Author: "Jasenko Zivanov"
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

#ifndef MAGNIFICATION_REFINEMENT_H
#define MAGNIFICATION_REFINEMENT_H

#include <src/ctf.h>
#include <src/image.h>
#include <src/metadata_table.h>
#include <src/jaz/optimization/optimization.h>
#include <src/jaz/volume.h>
#include <src/jaz/gravis/t2Matrix.h>
#include <vector>
#include "src/jaz/obs_model.h"

#include "equation2x2.h"

namespace MagnificationHelper {

    Matrix<RFLOAT> polarToMatrix(
        double scaleMajor = 1.0,
        double scaleMinor = 1.0,
        double angleDeg = 0.0);

    void matrixToPolar(
        const Matrix<RFLOAT> &mat,
        RFLOAT &scaleMajor,
        RFLOAT &scaleMinor,
        RFLOAT &angleDeg);

    void matrixToPolar(
        const gravis::d2Matrix &mat,
        RFLOAT &scaleMajor,
        RFLOAT &scaleMinor,
        RFLOAT &angleDeg);

    void updateScaleFreq(
        const Image<Complex> &prediction,
        const Volume<gravis::t2Vector<Complex>> &predGradient,
        const Image<Complex> &observation,
        CTF &ctf, ObservationModel *obsModel,
        double angpix,
        Volume<Equation2x2> &eqs,
        bool do_ctf_padding = false);

    void updateScaleReal(
        const Image<Complex> &prediction,
        const Image<Complex> &observation,
        const Image<RFLOAT> &snr,
        CTF &ctf, ObservationModel *obsModel,
        double angpix,
        Volume<Equation2x2> &eqs,
        bool do_ctf_padding = false);

    void solvePerPixel(
        const Volume<Equation2x2> &eqs,
        Image<RFLOAT> &vx, Image<RFLOAT> &vy);

    Matrix<RFLOAT> solveLinearlyFreq(
        const Volume<Equation2x2> &eqs,
        const Image<RFLOAT> &snr,
        Image<RFLOAT> &vx, Image<RFLOAT> &vy);

    void readEQs(std::string path, Volume<Equation2x2> &eqs);
    void writeEQs(const Volume<Equation2x2> &eqs, std::string path);

    void updatePowSpec(
        const Image<Complex> &prediction,
        const Image<Complex> &observation,
        CTF &ctf, ObservationModel *obsModel,
        double angpix,
        Image<RFLOAT> &powSpecPred,
        Image<RFLOAT> &powSpecObs,
        bool do_ctf_padding = false);

    void adaptAstigmatism(
        const std::vector<Matrix<RFLOAT>> &dMs,
        std::vector<MetaDataTable> &partMdts,
        bool perParticle, ObservationModel* obsModel);

};

#endif
