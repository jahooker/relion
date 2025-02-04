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

#ifndef OBS_MODEL_H
#define OBS_MODEL_H

#include <src/image.h>
#include <src/fftw.h>
#include <src/complex.h>
#include <src/metadata_table.h>
#include <src/projector.h>
#include <src/jaz/gravis/t2Matrix.h>

class BackProjector;

class ObservationModel {

    public:
    // tablename can be "particles", "micrographs" or "movies".
    // If tablename is "discover", the function will try to read the data table with all three names (in that order).
    static void loadSafely(std::string filename, ObservationModel& obsModel,
                            MetaDataTable& particlesMdt, std::string tablename = "particles", int verb = 0, bool do_die_upon_error = true);

    static void saveNew(MetaDataTable& particlesMdt, MetaDataTable& opticsMdt,
                        std::string filename, std::string _tablename = "particles");

    void save(MetaDataTable& particlesMdt, std::string filename, std::string _tablename = "particles");

    static bool containsAllColumnsNeededForPrediction(const MetaDataTable& partMdt);

        ObservationModel();
        ObservationModel(const MetaDataTable &opticsMdt, bool do_die_upon_error = true);

    MetaDataTable opticsMdt;
    bool hasEvenZernike, hasOddZernike, hasMagMatrices, hasBoxSizes, hasMultipleMtfs;

    void magnify(RFLOAT &X, RFLOAT &Y, const Matrix<RFLOAT> &M) {
        if (hasMagMatrices) {
            RFLOAT Xd = M(0, 0) * X + M(0, 1) * Y;
            RFLOAT Yd = M(1, 0) * X + M(1, 1) * Y;
            X = Xd;
            Y = Yd;
        }
    }

    protected:

    // cached values - protected to prevent users from accidentally changing them,
    // expecting the changes to propagate into the optics star-file
    std::vector<double> angpix, originalAngpix, lambda, Cs;
    std::vector<int> boxSizes;
    std::vector<bool> CtfPremultiplied;
    std::vector<std::vector<double>> evenZernikeCoeffs, oddZernikeCoeffs;
    std::vector<Matrix<RFLOAT>> magMatrices;
    std::vector<std::string> fnMtfs, groupNames;

    // cached aberration effects for a set of given image sizes
    // e.g.: phaseCorr[opt. group][img. height](y,x)
    std::vector<std::map<int,Image<Complex>>> phaseCorr;
    std::vector<std::map<int,Image<RFLOAT>>> gammaOffset, mtfImage;
    std::map<int,Image<RFLOAT>> avgMtfImage;

    public:

    // Prediction //
    MultidimArray<Complex> predictObservation(const Projector &proj, const MetaDataTable &partMdt,
                            long int particle, double angpix_ref,
                            bool applyCtf = true, bool shiftPhases = true, bool applyShift = true, bool applyMtf = true,
                            bool applyCtfPadding = false);

    Volume<gravis::t2Vector<Complex>> predictComplexGradient(Projector &proj, const MetaDataTable &partMdt,
                                                                long int particle, double angpix_ref,
                                                                bool applyCtf = true, bool shiftPhases = true, bool applyShift = true,
                                                                bool applyMtf = true,	bool applyCtfPadding = false);

    // Correction //

    void multiplyByMtf(int opticsGroup, MultidimArray<Complex>& obsImage,
        bool do_correct_average_mtf = true);

    // divide by MTF of detector (using cache)
    void divideByMtf(int opticsGroup, MultidimArray<Complex>& obsImage,
        bool do_correct_average_mtf = true);

    // 2D image with the MTF (cached)
    const Image<RFLOAT>& getMtfImage(int optGroup, int s);

    // 2D image with the average MTF (cached)
    const Image<RFLOAT>& getAverageMtfImage(int s);

    // apply effect of antisymmetric aberration (using cache)
    void modulatePhase(int optGroup, MultidimArray<Complex>& obsImage);

    void demodulatePhase(int optGroup, MultidimArray<Complex>& obsImage);

    // effect of antisymmetric aberration (cached)
    const Image<Complex>& getPhaseCorrection(int optGroup, int s);

    // effect of symmetric aberration (cached)
    const Image<RFLOAT>& getGammaOffset(int optGroup, int s);

    double scaleDifference(int opticsGroup, int s3D, double angpix3D);

    // Bureaucracy

    inline bool allPixelSizesIdentical() const {
        return std::adjacent_find(
            angpix.begin(), angpix.end(), std::not_equal_to<double>()
        ) == angpix.end();
    }

    inline bool allBoxSizesIdentical() const {
        return std::adjacent_find(
            boxSizes.begin(), boxSizes.end(), std::not_equal_to<int>()
        ) == boxSizes.end();
    }

    double angToPix(double a, int s, int opticsGroup) const;
    double pixToAng(double p, int s, int opticsGroup) const;

    double getPixelSize(int opticsGroup) const;
    std::vector<double> getPixelSizes() const;

    double getWavelength(int opticsGroup) const;
    std::vector<double> getWavelengths() const;

    double getSphericalAberration(int opticsGroup) const;
    std::vector<double> getSphericalAberrations() const;

    int getBoxSize(int opticsGroup) const;
    void getBoxSizes(std::vector<int>& sDest, std::vector<int>& shDest) const;

    // These do NOT update the metadata table!
    // These are only to change prediction etc.
    void setBoxSize(int opticsGroup, int newBoxSize);
    void setPixelSize(int opticsGroup, RFLOAT newPixelSize);

    Matrix<RFLOAT> getMagMatrix(int opticsGroup) const;
    std::vector<Matrix<RFLOAT>> getMagMatrices() const;
    void setMagMatrix(int opticsGroup, const Matrix<RFLOAT>& M);

    Matrix<RFLOAT> anisoMag(int opticsGroup) const;

    // 0-indexed
    int getOpticsGroup(const MetaDataTable &particlesMdt, long int particle = -1) const;

    bool getCtfPremultiplied(int og) const;
    void setCtfPremultiplied(int og, bool val);

    std::string getGroupName(int og);

    bool allPixelAndBoxSizesIdentical(const MetaDataTable& mdt);
    bool containsGroup(const MetaDataTable& mdt, int group);

    /* duh */
    int numberOfOpticsGroups() const;

    /* Check whether the optics groups appear in the correct order.
        This makes it possible to access a group g through:

            opticsMdt.getValue(label, g-1);
    */
    bool opticsGroupsSorted() const;

    /* Find all optics groups used in particles table partMdt
        that are not defined in opticsMdt (should return an empty vector) */
    std::vector<int> findUndefinedOptGroups(const MetaDataTable& partMdt) const;

    /* Rename optics groups to enforce the correct order
        and translate the indices in particle table partMdt.
        (Merely changing the order in opticsMdt would fail if groups were missing.) */
    void sortOpticsGroups(MetaDataTable& partMdt);

    /* Return the set of optics groups present in partMdt */
    std::vector<int> getOptGroupsPresent(const MetaDataTable& partMdt) const;

    std::vector<std::pair<int, std::vector<int>>> splitParticlesByOpticsGroup(const MetaDataTable& partMdt) const;

    private:

    template <typename Operator>
    void operateByMtf(
        int opticsGroup, MultidimArray<Complex> &obsImage,
        bool do_correct_average_mtf, Operator assign
    );

    template <typename F>
    void operatePhase(int optGroup, MultidimArray<Complex>& obsImage, F f);

};
#endif
