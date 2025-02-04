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

#ifndef SRC_GUI_MAINWINDOW_H_
#define SRC_GUI_MAINWINDOW_H_

// This #define / #undef pair protects against another Complex definition in fltk.
#define Complex
#include <FL/Fl_Scroll.H>
#undef Complex

#include "src/gui_jobwindow.h"
#include "src/pipeliner.h"
#include "src/scheduler.h"

#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>

// Sizing
const int JOBCOLWIDTH = 250;
const int XJOBCOL1 = 10;
const int XJOBCOL2 = JOBCOLWIDTH + 25;
const int XJOBCOL3 = 2 * JOBCOLWIDTH + 40;
const int JOBHEIGHT = 170;
const int JOBHALFHEIGHT = JOBHEIGHT / 2;
const int STDOUT_Y = 60;
const int STDERR_Y = 170;

enum { DONT_WRITE,       DO_WRITE       };
enum { DONT_READ,        DO_READ        };
enum { DONT_TOGGLE_CONT, DO_TOGGLE_CONT };
enum { DONT_GET_CL,      DO_GET_CL      };
enum { DONT_MKDIR,       DO_MKDIR       };

// Font size of browser windows on the main GUI
const int RLN_FONTSIZE = 13;

// Maximum number of jobs in the job-browsers in the pipeline-part of the GUI
const int MAX_JOBS_BROWSER = 50;

// This class organises the main window of the relion GUI
static Fl_Hold_Browser *browser;
static Fl_Group *browse_grp[NR_BROWSE_TABS];
static Fl_Group *background_grp;
static Fl_Group *pipeliner_jobs_grp;
static Fl_Group *pipeliner_grp;
static Fl_Group *scheduler_grp;
static Fl_Group *scheduler_run_grp;
static Fl_Group *scheduler_jobs_grp;
static Fl_Group *expand_stdout_grp;
static Fl_Choice *display_io_node;
static Fl_Select_Browser *finished_job_browser, *running_job_browser, *scheduled_job_browser, *input_job_browser, *output_job_browser;
static Fl_Box *image_box;
static Fl_Pixmap *xpm_image;
// For keeping track of which process to use in the process browser on the GUI
static std::vector<long int> running_processes, finished_processes, scheduled_processes, input_processes, output_processes, io_nodes;
static bool is_main_continue;
static bool do_overwrite_continue;

static JobWindow *gui_jobwindows[NR_BROWSE_TABS];

// Run button
// Sjors 16feb2018: somehow suddenly this run_button needs to be a non-static: otherwise it doesn't change to 'continue now' and doesnt grey out...
static Fl_Button *run_button;
static Fl_Button *print_CL_button;
static Fl_Button *schedule_button;
static Fl_Button *expand_stdout_button;
static Fl_Input *alias_current_job;

// Sjors 27May2019: scheduler
static Fl_Input *scheduler_job_name;
static Fl_Button *add_job_button;
static Fl_Choice *scheduler_job_mode, *scheduler_job_has_started;
static Fl_Menu_Item job_mode_options[] = {{"new"}, {"continue"}, {"overwrite"}, {0}};
static Fl_Menu_Item job_has_started_options[] = {{"has started"}, {"has not started"}, {0}};
// Scheduler variables
static Fl_Hold_Browser *scheduler_variable_browser;
static Fl_Button *set_scheduler_variable_button, *add_scheduler_operator_button;
static Fl_Button *delete_scheduler_variable_button, *delete_scheduler_operator_button;
static Fl_Input *scheduler_variable_name, *scheduler_variable_value;
//Scheduler Operators
static Fl_Hold_Browser *scheduler_operator_browser;
static std::vector<std::string> operators_list;
static Fl_Menu_Item operator_type_options[] = {
    {Schedule::FLOAT_OPERATOR_SET}, {Schedule::FLOAT_OPERATOR_PLUS}, 
    {Schedule::FLOAT_OPERATOR_MINUS}, {Schedule::FLOAT_OPERATOR_MULT}, 
    {Schedule::FLOAT_OPERATOR_DIVIDE}, {Schedule::FLOAT_OPERATOR_ROUND}, 
    {Schedule::FLOAT_OPERATOR_COUNT_IMAGES}, {Schedule::FLOAT_OPERATOR_COUNT_WORDS}, 
    {Schedule::FLOAT_OPERATOR_READ_STAR}, {Schedule::FLOAT_OPERATOR_READ_STAR_TABLE_MAX}, 
    {Schedule::FLOAT_OPERATOR_READ_STAR_TABLE_MIN}, {Schedule::FLOAT_OPERATOR_READ_STAR_TABLE_AVG}, 
    {Schedule::FLOAT_OPERATOR_READ_STAR_TABLE_SORT_IDX}, {Schedule::BOOLEAN_OPERATOR_AND}, 
    {Schedule::BOOLEAN_OPERATOR_OR}, {Schedule::BOOLEAN_OPERATOR_NOT}, 
    {Schedule::BOOLEAN_OPERATOR_GT}, {Schedule::BOOLEAN_OPERATOR_LT}, 
    {Schedule::BOOLEAN_OPERATOR_EQ}, {Schedule::BOOLEAN_OPERATOR_GE}, 
    {Schedule::BOOLEAN_OPERATOR_LE}, {Schedule::BOOLEAN_OPERATOR_FILE_EXISTS}, 
    {Schedule::BOOLEAN_OPERATOR_READ_STAR}, {Schedule::STRING_OPERATOR_JOIN}, 
    {Schedule::STRING_OPERATOR_BEFORE_FIRST}, {Schedule::STRING_OPERATOR_BEFORE_LAST}, 
    {Schedule::STRING_OPERATOR_AFTER_FIRST}, {Schedule::STRING_OPERATOR_AFTER_LAST}, 
    {Schedule::STRING_OPERATOR_READ_STAR}, {Schedule::STRING_OPERATOR_GLOB}, 
    {Schedule::STRING_OPERATOR_NTH_WORD}, {Schedule::OPERATOR_TOUCH_FILE}, 
    {Schedule::OPERATOR_COPY_FILE}, {Schedule::OPERATOR_MOVE_FILE}, 
    {Schedule::OPERATOR_DELETE_FILE}, {Schedule::WAIT_OPERATOR_SINCE_LAST_TIME}, 
    {Schedule::EMAIL_OPERATOR}, {Schedule::EXIT_OPERATOR}, 
    {0}
};
static Fl_Choice *scheduler_operator_type, *scheduler_operator_output, *scheduler_operator_input1, *scheduler_operator_input2;
// Scheduler jobs
static Fl_Hold_Browser *scheduler_job_browser, *scheduler_input_job_browser, *scheduler_output_job_browser;
static Fl_Button *scheduler_delete_job_button;

//Scheduler Edges
static Fl_Choice *scheduler_edge_input, *scheduler_edge_output, *scheduler_edge_boolean, *scheduler_edge_outputtrue;
static Fl_Hold_Browser *scheduler_edge_browser;
static Fl_Button *delete_scheduler_edge_button, *add_scheduler_edge_button;
// Scheduler current state
static Fl_Choice *scheduler_current_node;
static Fl_Button *scheduler_run_button, *scheduler_reset_button, *scheduler_set_current_button;
static Fl_Button *scheduler_next_button, *scheduler_prev_button;
static Fl_Button *scheduler_abort_button, *scheduler_unlock_button;

static Fl_Text_Buffer *textbuff_stdout;
static Fl_Text_Buffer *textbuff_stderr;

static void Gui_Timer_CB(void *userdata);

// Read-only GUI?
static bool maingui_do_read_only;

// Show the scheduler view
extern bool show_scheduler;

// Show expand stdout view
extern bool show_expand_stdout;

// The pipeline this GUI is acting on
static PipeLine pipeline;

// The current Scheduler
static Schedule schedule;

// Which is the current job being displayed?
static int current_job;
static FileName global_outputname;

// Order jobs in finished window alphabetically?
static bool do_order_alphabetically;

// The last time something changed
static time_t time_last_change;

/**
 * If we give our GroupContext a name, 
 * it will be destroyed at the end of the scope in which we declared it.
 * @code
 * {
 *     GroupContext context (grp);  // grp->begin() called
 *     do_something();
 * }  // grp->end() called
 * @endcode
 *
 * If we don't give our GroupContext a name, 
 * it will be destroyed immediately.
 * @code
 * GroupContext (grp);  // grp->begin() and grp->end() called in succession
 * @endcode
 *
 */
struct GroupContext {

    Fl_Group *&group;

    GroupContext(Fl_Group *&group): group(group) {
        group->begin();
    }

    ~GroupContext() {
        group->end();
    }

};

// Stdout and stderr display
class StdOutDisplay: public Fl_Text_Display {

    public:

        std::string fn_file;
        StdOutDisplay(int X, int Y, int W, int H, const char *l = 0): Fl_Text_Display(X, Y, W, H, l) {};
        ~StdOutDisplay() {};
        int handle(int ev);

};

static StdOutDisplay *disp_stdout, *disp_expand_stdout;
static StdOutDisplay *disp_stderr, *disp_expand_stderr;

class NoteEditorWindow: public Fl_Window {

    public:

    FileName fn_note;
    Fl_Text_Editor *editor;
    Fl_Text_Buffer *textbuff_note;
    bool allow_save;
    NoteEditorWindow(int w, int h, const char* t, FileName _fn_note, bool _allow_save = true);

    ~NoteEditorWindow() {};

    private:

    static void cb_save(Fl_Widget*, void*);
    inline void save();

    static void cb_cancel(Fl_Widget*, void*);
    inline void cancel();

};


class SchedulerWindow: public Fl_Window {

    public:

    FileName pipeline_name; // (e.g. default)
    std::vector<Fl_Check_Button*> check_buttons;
    Fl_Input *repeat, *wait_before, *wait, *schedule_name, *wait_after;
    std::vector<FileName> my_jobs; // Jobs to execute

    SchedulerWindow(int w, int h, const char* title): Fl_Window(w, h, title) {}

    ~SchedulerWindow() {};

    int fill(FileName _pipeline_name, std::vector<FileName> _scheduled_jobs);

    private:

    static void cb_execute(Fl_Widget*, void*);
    inline void cb_execute_i();

    static void cb_cancel(Fl_Widget*, void*);
    inline void cb_cancel_i();

};

/*
class SchedulerAddVariableOperatorWindow: public Fl_Window {

    public:

    Fl_Input *name, *value, *type, *input1, *input2, *output;

    SchedulerAddVariableOperatorWindow(int w, int h, const char* title): Fl_Window(w, h, title){}

    ~SchedulerAddVariableOperatorWindow() {};

    int fill(bool is_variable, bool is_add);

    private:

    static void cb_add(Fl_Widget*, void*);
    inline void cb_add_i();

    static void cb_cancel(Fl_Widget*, void*);
    inline void cb_cancel_i();

};
*/

class GuiMainWindow: public Fl_Window {

    public:

    // For Tabs
    Fl_Menu_Bar *menubar, *menubar2;

    // For clicking in stdout/err windows
    StdOutDisplay *stdoutbox, *stderrbox;

    // Update GUI every how many seconds
    int update_every_sec;

    // Exit GUI after how many seconds idle?
    float exit_after_sec;

    // For job submission
    std::string final_command;
    std::vector<std::string> commands;

    // Constructor with w × h size of the window and a title
    GuiMainWindow(
        int w, int h, const char* title, FileName fn_pipe, FileName fn_sched, 
        int _update_every_sec, int _exit_after_sec, bool _do_read_only = false
    );

    // Destructor
    ~GuiMainWindow(){ clear(); };

    // Clear stuff
    void clear();

    // How will jobs be displayed in the GUI job running, finished, in, out & scheduled job lists
    std::string getJobNameForDisplay(Process &job);

    // Update the content of the finished, running and scheduled job lists
    void fillRunningJobLists();

    // Update the content of the input and output job lists for the current job
    void fillToAndFromJobLists();

    void fillSchedulerNodesAndVariables();

    // Need public access for auto-updating the GUI
    void fillStdOutAndErr();

    // Touch the TimeStamp of the last change
    void tickTimeLastChanged();

    // Update all job lists (running, scheduled, finished, as well as to/from)
    void updateJobLists();

    // When a job is selected from the job browsers at the bottom: set current_job there, load that one in the current window
    // and update all job lists at the bottom
    void loadJobFromPipeline(int this_job);

    // Run scheduled jobs from the pipeliner
    void runScheduledJobs(FileName fn_sched, FileName fn_jobids, int nr_repeat, long int minutes_wait);

    private:

    // Vertical distance from the top
    int start_y;

    // Current height
    int current_y;

    /** Callback functions
     *  The method of using two functions of static void and inline void was copied from:
     *  http://www3.telus.net/public/robark/
     */

    /// NOTE: There must be a better way!

    static void cb_select_browsegroup(Fl_Widget*, void*);
    inline void cb_select_browsegroup_i(bool is_initial = false);

    static void cb_select_finished_job(Fl_Widget*, void*);
    inline void cb_select_finished_job_i();

    static void cb_select_running_job(Fl_Widget*, void*);
    inline void cb_select_running_job_i();

    static void cb_select_scheduled_job(Fl_Widget*, void*);
    inline void cb_select_scheduled_job_i();

    static void cb_select_input_job(Fl_Widget*, void*);
    inline void cb_select_input_job_i();

    static void cb_select_output_job(Fl_Widget*, void*);
    inline void cb_select_output_job_i();

    static void cb_display_io_node(Fl_Widget*, void*);
    inline void cb_display_io_node_i();

    static void cb_add_scheduler_edge(Fl_Widget*, void*);
    inline void cb_add_scheduler_edge_i();

    static void cb_delete_scheduler_edge(Fl_Widget*, void*);
    inline void cb_delete_scheduler_edge_i();

    static void cb_select_scheduler_edge(Fl_Widget*, void*);
    inline void cb_select_scheduler_edge_i();

    static void cb_set_scheduler_variable(Fl_Widget*, void*);
    inline void cb_set_scheduler_variable_i();

    static void cb_delete_scheduler_variable(Fl_Widget*, void*);
    inline void cb_delete_scheduler_variable_i();

    static void cb_select_scheduler_variable(Fl_Widget*, void*);
    inline void select_scheduler_variable();

    static void cb_add_scheduler_operator(Fl_Widget*, void*);
    inline void cb_add_scheduler_operator_i();

    static void cb_delete_scheduler_operator(Fl_Widget*, void*);
    inline void cb_delete_scheduler_operator_i();

    static void cb_select_scheduler_operator(Fl_Widget*, void*);
    inline void cb_select_scheduler_operator_i();

    static void cb_delete_scheduler_job(Fl_Widget*, void*);
    inline void cb_delete_scheduler_job_i();

    static void cb_scheduler_add_job(Fl_Widget*, void*);
    inline void cb_scheduler_add_job_i();

    static void cb_scheduler_set_current(Fl_Widget*, void*);
    inline void scheduler_set_current();

    static void cb_scheduler_next(Fl_Widget*, void*);
    inline void cb_scheduler_next_i();

    static void cb_scheduler_prev(Fl_Widget*, void*);
    inline void cb_scheduler_prev_i();

    static void cb_scheduler_unlock(Fl_Widget*, void*);
    inline void cb_scheduler_unlock_i();

    static void cb_scheduler_abort(Fl_Widget*, void*);
    inline void cb_scheduler_abort_i();

    static void cb_scheduler_reset(Fl_Widget*, void*);
    inline void cb_scheduler_reset_i();

    static void cb_scheduler_run(Fl_Widget*, void*);
    inline void cb_scheduler_run_i();

    static void cb_display(Fl_Widget*, void*);
    inline void cb_display_i();

    inline void cb_toggle_continue_i();

    static void cb_run(Fl_Widget*, void*);
    static void cb_schedule(Fl_Widget*, void*);
    inline void run(bool only_schedule = false, bool do_open_edit = true);

    static void cb_delete(Fl_Widget*, void*);
    inline void cb_delete_i(bool ask = true, bool recursively = true);

    static void cb_gently_clean_all_jobs(Fl_Widget*, void*);
    static void cb_harshly_clean_all_jobs(Fl_Widget*, void*);
    inline void clean_all_jobs(bool harshly);

    static void cb_gentle_cleanup(Fl_Widget*, void*);
    static void cb_harsh_cleanup(Fl_Widget*, void*);
    inline void cleanup(int jobindex = -1, bool ask = true, bool harshly = false);

    static void cb_set_alias(Fl_Widget*, void*);
    inline void set_alias(std::string newalias = "");

    static void cb_abort(Fl_Widget*, void*);
    inline void cb_abort_i(std::string newalias = "");

    static void cb_mark_as_finished(Fl_Widget*, void*);
    static void cb_mark_as_failed(Fl_Widget*, void*);
    inline void mark_as_finished(bool is_failed = false);

    static void cb_make_flowchart(Fl_Widget*, void*);
    inline void cb_make_flowchart_i();

    static void cb_edit_project_note(Fl_Widget*, void*);
    static void cb_edit_note(Fl_Widget*, void*);
    inline void cb_edit_note_i(bool is_project_note = false);

    static void cb_print_cl(Fl_Widget*, void*);
    inline void cb_print_cl_i();

    static void cb_save(Fl_Widget*, void*);
    inline void save();

    static void cb_load(Fl_Widget*, void*);
    inline void load();

    static void cb_undelete_job(Fl_Widget*, void*);
    inline void cb_undelete_job_i();

    static void cb_export_jobs(Fl_Widget*, void*);
    inline void cb_export_jobs_i();

    static void cb_import_jobs(Fl_Widget*, void*);
    inline void cb_import_jobs_i();

    static void cb_order_jobs_alphabetically(Fl_Widget*, void*);
    static void cb_order_jobs_chronologically(Fl_Widget*, void*);

    static void cb_empty_trash(Fl_Widget*, void*);
    inline void cb_empty_trash_i();

    static void cb_print_notes(Fl_Widget*, void*);
    inline void cb_print_notes_i();

    static void cb_remake_nodesdir(Fl_Widget*, void*);
    inline void cb_remake_nodesdir_i();

    static void cb_reread_pipeline(Fl_Widget*, void*);
    inline void cb_reread_pipeline_i();

    static void cb_reactivate_runbutton(Fl_Widget*, void*);
    inline void cb_reactivate_runbutton_i();

    static void cb_toggle_overwrite_continue(Fl_Widget*, void*);
    inline void cb_toggle_overwrite_continue_i();

    static void cb_show_initial_screen(Fl_Widget*, void*);
    inline void cb_show_initial_screen_i();

    static void cb_toggle_pipeliner_scheduler(Fl_Widget*, void*);
    inline void cb_toggle_pipeliner_scheduler_i();

    static void cb_copy_schedule(Fl_Widget*, void*);
    static void cb_toggle_schedule(Fl_Widget*, void*);
    static void cb_toggle_pipeline(Fl_Widget*, void*);
    static void cb_create_schedule(Fl_Widget*, void*);
    inline void cb_toggle_schedule_i(bool do_pipeline, FileName fn_new_schedule = "");

    static void cb_start_pipeliner(Fl_Widget*, void*);
    inline void cb_start_pipeliner_i();

    static void cb_stop_pipeliner(Fl_Widget*, void*);
    inline void cb_stop_pipeliner_i();

    static void cb_toggle_expand_stdout(Fl_Widget*, void*);
    inline void toggle_expand_stdout();

    static void cb_about(Fl_Widget*, void*);

    public:

    static void cb_quit(Fl_Widget*, void*);

};

#endif
