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
#ifndef SRC_PIPELINE_JOBS_H_
#define SRC_PIPELINE_JOBS_H_

#include "src/macros.h"
#include "src/metadata_table.h"
#include "src/filename.h"
#include <string>
using std::string;
#include <vector>
using std::vector;
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <fstream>

namespace JOBOPTION { enum {
    UNDEFINED,
    ANY,
    FILENAME,
    INPUTNODE,
    RADIO,
    BOOLEAN,
    SLIDER,
    ONLYTEXT
}; };

enum {
    TOGGLE_DEACTIVATE,
    TOGGLE_REACTIVATE,
    TOGGLE_ALWAYS_DEACTIVATE,
    TOGGLE_LEAVE_ACTIVE
};

const bool HAS_MPI = true;
const bool HAS_THREAD = true;

enum {
    RADIO_SAMPLING,
    RADIO_NODETYPE,
    RADIO_GAIN_ROTATION,
    RADIO_GAIN_FLIP
};

// Hard-coded LMB defaults
namespace DEFAULT {
    const char* const PDFVIEWER = "evince";
    const char *const QSUBLOCATION = "/public/EM/RELION/relion/bin/relion_qsub.csh";
    const char *const CTFFINDLOCATION = "/public/EM/ctffind/ctffind.exe";
    const char *const MOTIONCOR2LOCATION = "/public/EM/MOTIONCOR2/MotionCor2";
    const char *const GCTFLOCATION = "/public/EM/Gctf/bin/Gctf";
    const char *const RESMAPLOCATION = "/public/EM/ResMap/ResMap-1.1.4-linux64";
    const char *const QSUBCOMMAND = "qsub";
    const char *const QUEUENAME = "openmpi";
    const int MINIMUMDEDICATED = 1;
    const int WARNINGLOCALMPI = 32;
    const bool ALLOWCHANGEMINDEDICATED = true;
    const bool QUEUEUSE = false;
    const int NRMPI = 1;
    const int MPIMAX = 64;
    const int NRTHREADS = 1;
    const int THREADMAX = 16;
    const char *const MPIRUN = "mpirun";
    const char *const SCRATCHDIR = "";
};

static const vector<string> job_undefined_options {
    "undefined"
};

static const vector<string> job_boolean_options {
    "Yes",
    "No"
};

static const vector<string> job_sampling_options {
    "30 degrees",
    "15 degrees",
    "7.5 degrees",
    "3.7 degrees",
    "1.8 degrees",
    "0.9 degrees",
    "0.5 degrees",
    "0.2 degrees",
    "0.1 degrees"
};
// Modify the loop in JobOption::getHealPixOrder when you add more choices!


static const vector<string> job_nodetype_options {
    "Particle coordinates (*.box, *_pick.star)",
    "Particles STAR file (.star)",
    "Movie-particles STAR file (.star)",
    "2D references (.star or .mrcs)",
    "Micrographs STAR file (.star)",
    "3D reference (.mrc)",
    "3D mask (.mrc)",
    "Unfiltered half-map (unfil.mrc)"
};

static const vector<string> job_gain_rotation_options {
    "No rotation (0)",
    "90 degrees (1)",
    "180 degrees (2)",
    "270 degrees (3)"
};

static const vector<string> job_gain_flip_options {
    "No flipping (0)",
    "Flip upside down (1)",
    "Flip left to right (2)"
};

static const vector<string> job_ctffit_options {
    "No",
    "Per-micrograph",
    "Per-particle"
};

// To have a line on the GUI to change the minimum number of dedicated in a job
static bool do_allow_change_minimum_dedicated;

// Optional output file for any jobtype that explicitly defines the output nodes
const char *const RELION_OUTPUT_NODES = "RELION_OUTPUT_NODES.star";

const int NR_BROWSE_TABS = 20;

struct gui_layout {
    string tabname;
    int ypos;
    RFLOAT w;
};

// Get the other half map by swapping half1 and half2
FileName getTheOtherHalf(const FileName &fn_half1);

/*
 * The Node class represents data and metadata that are either input to or output from Processes
 * Nodes are connected to each by Edges:
 * - the fromEdgeList are connections with Nodes earlier (higher up) in the pipeline
 * - the toEdgeList are connections with Nodes later (lower down) in the pipeline
 */
class Node {

    public:

    string name;
    int type;
    // list of processes that use this Node as input
    vector<long int> inputForProcessList;
    // Which process made this Node
    long int outputFromProcess;

    // Constructor
    Node(string _name, int _type) {
        name = _name;
        type = _type;
        outputFromProcess = -1;
    }

    // Destructor
    ~Node() {
        // Do not delete the adjacent nodes here... They will be deleted by graph destructor
        inputForProcessList.clear();
    }

    // Nodes can be of the following types:
    enum Types {
        MOVIES, // 2D micrograph movie(s), e.g. Falcon001_movie.mrcs or micrograph_movies.star
        MICS,  // 2D micrograph(s), possibly with CTF information as well, e.g. Falcon001.mrc or micrographs.star
        MIC_COORDS,  // Suffix for particle coordinates in micrographs (e.g. autopick.star or .box)
        PART_DATA,  // A metadata (STAR) file with particles (e.g. particles.star or run1_data.star)
        // MOVIE_DATA,  // A metadata (STAR) file with particle movie-frames (e.g. particles_movie.star or run1_ct27_data.star)
        REFS2D,  // A STAR file with one or multiple 2D references, e.g. autopick_references.star
        REF3D,  // A single 3D-reference, e.g. map.mrc
        MASK,  // 3D mask, e.g. mask.mrc or masks.star
        MODEL,  // A model STAR-file for class selection
        OPTIMISER,  // An optimiser STAR-file for job continuation
        HALFMAP,  // Unfiltered half-maps from 3D auto-refine, e.g. run1_half?_class001_unfil.mrc
        FINALMAP,  // Sharpened final map from post-processing (cannot be used as input)
        RESMAP,  // Resmap with local resolution (cannot be used as input)
        PDF_LOGFILE,  // PDF logfile
        POST,  // Postprocess STAR file (with FSC curve, unfil half-maps, masks etc in it: used by Jasenko's programs
        POLISH_PARAMS  // Txt file with optimal parameters for Bayesian polishing
    };

};

// Helper function to get the outputnames of refine jobs
vector<Node> getOutputNodesRefine(
    string outputname, int iter, int K, int dim, int nr_bodies = 1
);

// A class to store any type of Option for a GUI entry
class JobOption {

    public:

    // Get HealPix order from string. Return -1 on failure.
    static int getHealPixOrder(const string &s);

    // Get a f/p/m character for CTF fitting. Return "" on failutre.
    static string getCtfFitString(const string &option);

    public:

    string label;
    string label_gui;
    int joboption_type;
    string variable;
    string value;
    string default_value;
    string helptext;
    float min_value;
    float max_value;
    float step_value;
    int node_type;
    string pattern;
    string directory;
    vector<string> radio_options;

    public:

    // Any constructor
    JobOption(const string &_label, const string &_default_value, const string &_helptext);

    // FileName constructor
    JobOption(
        const string &_label, const string & _default_value, const string &_pattern,
        const string &_directory, const string &_helptext
    );

    // InputNode constructor
    JobOption(
        const string &_label, int _nodetype, const string &_default_value,
        const string &_pattern, const string &_helptext
    );

    // Radio constructor
    JobOption(
        const string &_label, const vector<string> &radio_options,
        int ioption, const string &_helptext
    );

    // Boolean constructor
    JobOption(const string &_label, bool _boolvalue, const string &_helptext);

    // Slider constructor
    JobOption(
        const string &_label, float _default_value, float _min_value,
        float _max_value, float _step_value, const string &_helptext
    );

    // Write to a STAR file
    void writeToMetaDataTable(MetaDataTable& MD) const;

    // Empty constructor
    JobOption() { clear(); }

    // Empty destructor
    ~JobOption() { clear(); }

    void clear();

    // Set values of label, value, default_value and helptext (common for all types)
    void initialise(const string &_label, const string &_default_value, const string &_helptext);

    // Contains $$ for SchedulerVariable
    bool isSchedulerVariable();

    // Get a string value
    string getString();

    // Set a string value
    void setString(const string &newvalue);

    // Get a string value
    Node getNode();

    // Get a numbered value
    float getNumber();

    // Get a boolean value
    bool getBoolean();

    // Read value from an ifstream. Return false on failure.
    bool readValue(std::ifstream& in);

    // Write value to an ostream
    // Format: label == value
    void writeValue(std::ostream& out);

};

class RelionJob {

    public:

    // The name of this job
    string outputName;

    // The alias to this job
    string alias;

    // Name of the hidden file
    string hidden_name;

    // Which job type is this?
    int type;

    // Is this a continuation job?
    bool is_continue;

    // List of Nodes of input to this process
    vector<Node> inputNodes;

    // List of Nodes of output from this process
    vector<Node> outputNodes;

    // All the options to this job
    std::map<string, JobOption> joboptions;

    public:

    // Constructor
    RelionJob() { clear(); }

    RelionJob(int job_type) {
        clear();
        initialise(job_type);
    }

    // Empty Destructor
    ~RelionJob() { clear(); }

    // Clear everything
    void clear() {
        outputName = alias = "";
        type = -1;
        inputNodes.clear();
        outputNodes.clear();
        joboptions.clear();
        is_continue = false;
    }

    // Returns true if the option is present in joboptions
    bool containsLabel(const string &label, string &option);

    // Set this option in the job
    void setOption(const string &setOptionLine);

    // Read/write settings from/to disc
    // fn is a directory name (e.g. Refine3D/job123/) or a STAR file
    bool read(
        const string &fn,
        bool &is_continue, bool do_initialise = false
    );

    // return false if unsuccessful
    void write(const string &fn);

    // Write the job submission script
    void saveJobSubmissionScript(
        const string &newfilename, const string &outputname,
        const vector<string> &commands
    );

    // Initialise pipeline stuff for each job, return outputname
    void initialisePipeline(
        string &outputname, const string &defaultname, int job_counter
    );

    // Prepare the final (job submission or combined (mpi) command of possibly multiple lines)
    // Returns true to go ahead, and false to cancel
    string prepareFinalCommand(
        const string &outputname, vector<string> &commands, bool do_makedir
    );

    // Initialise the generic RelionJob
    void initialise(int job_type);

    // Generic getCommands
    string getCommands(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    // Now all the specific job types are defined
    void initialiseImportJob();
    string getCommandsImportJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseMotioncorrJob();
    string getCommandsMotioncorrJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseCtffindJob();
    string getCommandsCtffindJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseManualpickJob();
    string getCommandsManualpickJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseAutopickJob();
    string getCommandsAutopickJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseExtractJob();
    string getCommandsExtractJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseSelectJob();
    string getCommandsSelectJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseClass2DJob();
    string getCommandsClass2DJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseInimodelJob();
    string getCommandsInimodelJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseClass3DJob();
    string getCommandsClass3DJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseAutorefineJob();
    string getCommandsAutorefineJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseMultiBodyJob();
    string getCommandsMultiBodyJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseMaskcreateJob();
    string getCommandsMaskcreateJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseJoinstarJob();
    string getCommandsJoinstarJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseSubtractJob();
    string getCommandsSubtractJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialisePostprocessJob();
    string getCommandsPostprocessJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseLocalresJob();
    string getCommandsLocalresJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseMotionrefineJob();
    string getCommandsMotionrefineJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseCtfrefineJob();
    string getCommandsCtfrefineJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

    void initialiseExternalJob();
    string getCommandsExternalJob(
        string &outputname, vector<string> &commands,
        bool do_makedir, int job_counter
    );

};

#endif /* SRC_PIPELINE_JOBS_H_ */
