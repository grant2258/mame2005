/************************************************************************
 *
 *  MAME - Discrete sound system emulation library
 *
 *  Written by Keith Wilkins (mame@esplexo.co.uk)
 *
 *  (c) K.Wilkins 2000
 *
 *  Coding started in November 2000
 *  KW - Added Sawtooth waveforms  Feb2003
 *
 ***********************************************************************
 *
 * SEE DISCRETE.H for documentation on usage
 *
 ***********************************************************************
 *
 * Each sound primative DSS_xxxx or DST_xxxx has its own implementation
 * file. All discrete sound primatives MUST implement the following
 * API:
 *
 * dsX_NAME_step(inputs, context,float timestep)  - Perform time step
 *                                                  return output value
 * dsX_NAME_reset(context) - Reset to initial state
 *
 * Core software takes care of traversing the netlist in the correct
 * order
 *
 * discrete_sh_start()       - Read Node list, initialise & reset
 * discrete_sh_stop()        - Shutdown discrete sound system
 * discrete_sh_reset()       - Put sound system back to time 0
 * discrete_sh_update()      - Update streams to current time
 * discrete_stream_update()  - This does the real update to the sim
 *
 ************************************************************************/

#include "driver.h"
#include "wavwrite.h"
#include "discrete.h"
#include <stdio.h>
#include <stdarg.h>
#include <math.h>


/*************************************
 *
 *	Debugging
 *
 *************************************/

#define DISCRETE_WAVELOG			(0)
#define DISCRETE_DEBUGLOG			(0)



/*************************************
 *
 *	Global variables
 *
 *************************************/

struct discrete_info
{
	/* internal node tracking */
	int node_count;
	struct node_description **running_order;
	struct node_description **indexed_node;
	struct node_description *node_list;

	/* output node tracking */
	int discrete_outputs;
	struct node_description *output_node[DISCRETE_MAX_OUTPUTS];

	/* the input streams */
	int discrete_input_streams;
	stream_sample_t *input_stream_data[DISCRETE_MAX_OUTPUTS];

	/* the output stream */
	sound_stream *discrete_stream;
};

static struct discrete_info *discrete_current_context;

/* debugging statics */
static wav_file *disc_wav_file[DISCRETE_MAX_OUTPUTS];
FILE *disclogfile = NULL;



/*************************************
 *
 *	Prototypes
 *
 *************************************/

static void init_nodes(struct discrete_info *info, struct discrete_sound_block *block_list);
static void find_input_nodes(struct discrete_info *info, struct discrete_sound_block *block_list);
static void setup_output_nodes(struct discrete_info *info);
static void discrete_reset(void *chip);



/*************************************
 *
 *	Debug logging
 *
 *************************************/

void CLIB_DECL discrete_log(const char *text, ...)
{
	if (DISCRETE_DEBUGLOG)
	{
		va_list arg;
		va_start(arg, text);

		if(disclogfile)
		{
			vfprintf(disclogfile, text, arg);
			fprintf(disclogfile, "\n");
		}

		va_end(arg);
	}
}



/*************************************
 *
 *	Included simulation objects
 *
 *************************************/

#include "disc_wav.c"		/* Wave sources   - SINE/SQUARE/NOISE/etc */
#include "disc_mth.c"		/* Math Devices   - ADD/GAIN/etc */
#include "disc_inp.c"		/* Input Devices  - INPUT/CONST/etc */
#include "disc_flt.c"		/* Filter Devices - RCF/HPF/LPF */
#include "disc_dev.c"		/* Popular Devices - NE555/etc */



/*************************************
 *
 *	Master module list
 *
 *************************************/

struct discrete_module module_list[] =
{
	{ DSO_OUTPUT      ,"DSO_OUTPUT"      ,0                                      ,NULL                  ,NULL                 },

	/* from disc_inp.c */
	{ DSS_ADJUSTMENT  ,"DSS_ADJUSTMENT"  ,sizeof(struct dss_adjustment_context)  ,dss_adjustment_reset  ,dss_adjustment_step  },
	{ DSS_CONSTANT    ,"DSS_CONSTANT"    ,0                                      ,NULL                  ,dss_constant_step    },
	{ DSS_INPUT_DATA  ,"DSS_INPUT_DATA"  ,sizeof(data8_t)                        ,dss_input_reset       ,dss_input_step       },
	{ DSS_INPUT_LOGIC ,"DSS_INPUT_LOGIC" ,sizeof(data8_t)                        ,dss_input_reset       ,dss_input_step       },
	{ DSS_INPUT_NOT   ,"DSS_INPUT_NOT"   ,sizeof(data8_t)                        ,dss_input_reset       ,dss_input_step       },
	{ DSS_INPUT_PULSE ,"DSS_INPUT_PULSE" ,sizeof(data8_t)                        ,dss_input_reset       ,dss_input_pulse_step },
	{ DSS_INPUT_STREAM,"DSS_INPUT_STREAM",sizeof(stream_sample_t)                ,dss_input_stream_reset,dss_input_stream_step},

	/* from disc_wav.c */
	/* Generic modules */
	{ DSS_COUNTER     ,"DSS_COUNTER"     ,sizeof(struct dss_counter_context)     ,dss_counter_reset     ,dss_counter_step     },
	{ DSS_LFSR_NOISE  ,"DSS_LFSR_NOISE"  ,sizeof(struct dss_lfsr_context)        ,dss_lfsr_reset        ,dss_lfsr_step        },
	{ DSS_NOISE       ,"DSS_NOISE"       ,sizeof(struct dss_noise_context)       ,dss_noise_reset       ,dss_noise_step       },
	{ DSS_NOTE        ,"DSS_NOTE"        ,sizeof(struct dss_note_context)        ,dss_note_reset        ,dss_note_step        },
	{ DSS_SAWTOOTHWAVE,"DSS_SAWTOOTHWAVE",sizeof(struct dss_sawtoothwave_context),dss_sawtoothwave_reset,dss_sawtoothwave_step},
	{ DSS_SINEWAVE    ,"DSS_SINEWAVE"    ,sizeof(struct dss_sinewave_context)    ,dss_sinewave_reset    ,dss_sinewave_step    },
	{ DSS_SQUAREWAVE  ,"DSS_SQUAREWAVE"  ,sizeof(struct dss_squarewave_context)  ,dss_squarewave_reset  ,dss_squarewave_step  },
	{ DSS_SQUAREWFIX  ,"DSS_SQUAREWFIX"  ,sizeof(struct dss_squarewfix_context)  ,dss_squarewfix_reset  ,dss_squarewfix_step  },
	{ DSS_SQUAREWAVE2 ,"DSS_SQUAREWAVE2" ,sizeof(struct dss_squarewave_context)  ,dss_squarewave2_reset ,dss_squarewave2_step },
	{ DSS_TRIANGLEWAVE,"DSS_TRIANGLEWAVE",sizeof(struct dss_trianglewave_context),dss_trianglewave_reset,dss_trianglewave_step},
	/* Component specific modules */
	{ DSS_OP_AMP_OSC  ,"DSS_OP_AMP_OSC"  ,sizeof(struct dss_op_amp_osc_context)  ,dss_op_amp_osc_reset  ,dss_op_amp_osc_step  },
	{ DSS_SCHMITT_OSC ,"DSS_SCHMITT_OSC" ,sizeof(struct dss_schmitt_osc_context) ,dss_schmitt_osc_reset ,dss_schmitt_osc_step },
	/* Not yet implemented */
	{ DSS_ADSR        ,"DSS_ADSR"        ,sizeof(struct dss_adsr_context)        ,dss_adsrenv_reset     ,dss_adsrenv_step     },

	/* from disc_mth.c */
	/* Generic modules */
	{ DST_ADDER       ,"DST_ADDER"       ,0                                      ,NULL                  ,dst_adder_step       },
	{ DST_CLAMP       ,"DST_CLAMP"       ,0                                      ,NULL                  ,dst_clamp_step       },
	{ DST_DIVIDE      ,"DST_DIVIDE"      ,0                                      ,NULL                  ,dst_divide_step      },
	{ DST_GAIN        ,"DST_GAIN"        ,0                                      ,NULL                  ,dst_gain_step        },
	{ DST_LOGIC_INV   ,"DST_LOGIC_INV"   ,0                                      ,NULL                  ,dst_logic_inv_step   },
	{ DST_LOGIC_AND   ,"DST_LOGIC_AND"   ,0                                      ,NULL                  ,dst_logic_and_step   },
	{ DST_LOGIC_NAND  ,"DST_LOGIC_NAND"  ,0                                      ,NULL                  ,dst_logic_nand_step  },
	{ DST_LOGIC_OR    ,"DST_LOGIC_OR"    ,0                                      ,NULL                  ,dst_logic_or_step    },
	{ DST_LOGIC_NOR   ,"DST_LOGIC_NOR"   ,0                                      ,NULL                  ,dst_logic_nor_step   },
	{ DST_LOGIC_XOR   ,"DST_LOGIC_XOR"   ,0                                      ,NULL                  ,dst_logic_xor_step   },
	{ DST_LOGIC_NXOR  ,"DST_LOGIC_NXOR"  ,0                                      ,NULL                  ,dst_logic_nxor_step  },
	{ DST_LOGIC_DFF   ,"DST_LOGIC_DFF"   ,sizeof(struct dst_dflipflop_context)   ,dst_logic_dff_reset   ,dst_logic_dff_step   },
	{ DST_MULTIPLEX   ,"DST_MULTIPLEX"   ,sizeof(struct dst_size_context)        ,dst_multiplex_reset   ,dst_multiplex_step   },
	{ DST_ONESHOT     ,"DST_ONESHOT"     ,sizeof(struct dst_oneshot_context)     ,dst_oneshot_reset     ,dst_oneshot_step     },
	{ DST_RAMP        ,"DST_RAMP"        ,sizeof(struct dss_ramp_context)        ,dst_ramp_reset        ,dst_ramp_step        },
	{ DST_SAMPHOLD    ,"DST_SAMPHOLD"    ,sizeof(struct dst_samphold_context)    ,dst_samphold_reset    ,dst_samphold_step    },
	{ DST_SWITCH      ,"DST_SWITCH"      ,0                                      ,NULL                  ,dst_switch_step      },
	{ DST_TRANSFORM   ,"DST_TRANSFORM"   ,0                                      ,NULL                  ,dst_transform_step   },
	/* Component specific */
	{ DST_COMP_ADDER  ,"DST_COMP_ADDER"  ,0                                      ,NULL                  ,dst_comp_adder_step  },
	{ DST_DAC_R1      ,"DST_DAC_R1"      ,sizeof(struct dst_dac_r1_context)      ,dst_dac_r1_reset      ,dst_dac_r1_step      },
	{ DST_DIODE_MIX   ,"DST_DIODE_MIX"   ,sizeof(struct dst_size_context)        ,dst_diode_mix_reset   ,dst_diode_mix_step   },
	{ DST_INTEGRATE   ,"DST_INTEGRATE"   ,sizeof(struct dst_integrate_context)   ,dst_integrate_reset   ,dst_integrate_step   },
	{ DST_MIXER       ,"DST_MIXER"       ,sizeof(struct dst_mixer_context)       ,dst_mixer_reset       ,dst_mixer_step       },
	{ DST_TVCA_OP_AMP ,"DST_TVCA_OP_AMP" ,sizeof(struct dst_tvca_op_amp_context) ,dst_tvca_op_amp_reset ,dst_tvca_op_amp_step },
	{ DST_VCA         ,"DST_VCA"         ,0                                      ,NULL                  ,NULL                 },

	/* from disc_flt.c */
	/* Generic modules */
	{ DST_FILTER1     ,"DST_FILTER1"     ,sizeof(struct dss_filter1_context)     ,dst_filter1_reset     ,dst_filter1_step     },
	{ DST_FILTER2     ,"DST_FILTER2"     ,sizeof(struct dss_filter2_context)     ,dst_filter2_reset     ,dst_filter2_step     },
	/* Component specific modules */
	{ DST_CRFILTER    ,"DST_CRFILTER"    ,sizeof(struct dst_rcfilter_context)    ,dst_crfilter_reset    ,dst_crfilter_step    },
	{ DST_OP_AMP_FILT ,"DST_OP_AMP_FILT" ,sizeof(struct dst_op_amp_filt_context) ,dst_op_amp_filt_reset ,dst_op_amp_filt_step },
	{ DST_RCDISC      ,"DST_RCDISC"      ,sizeof(struct dst_rcdisc_context)      ,dst_rcdisc_reset      ,dst_rcdisc_step      },
	{ DST_RCDISC2     ,"DST_RCDISC2"     ,sizeof(struct dst_rcdisc_context)      ,dst_rcdisc2_reset     ,dst_rcdisc2_step     },
	{ DST_RCDISC3     ,"DST_RCDISC3"     ,sizeof(struct dst_rcdisc_context)      ,dst_rcdisc3_reset     ,dst_rcdisc3_step     },
	{ DST_RCDISC4     ,"DST_RCDISC4"     ,sizeof(struct dst_rcdisc4_context)     ,dst_rcdisc4_reset     ,dst_rcdisc4_step     },
	{ DST_RCFILTER    ,"DST_RCFILTER"    ,sizeof(struct dst_rcfilter_context)    ,dst_rcfilter_reset    ,dst_rcfilter_step    },
	/* For testing - seem to be buggered.  Use versions not ending in N. */
	{ DST_RCFILTERN   ,"DST_RCFILTERN"   ,sizeof(struct dss_filter1_context)     ,dst_rcfilterN_reset   ,dst_filter1_step     },
	{ DST_RCDISCN     ,"DST_RCDISCN"     ,sizeof(struct dss_filter1_context)     ,dst_rcdiscN_reset     ,dst_rcdiscN_step     },
	{ DST_RCDISC2N    ,"DST_RCDISC2N"    ,sizeof(struct dss_rcdisc2_context)     ,dst_rcdisc2N_reset    ,dst_rcdisc2N_step    },

	/* from disc_dev.c */
	/* generic modules */
	{ DST_CUSTOM      ,"DST_CUSTOM"      ,0                                      ,NULL                  ,dst_transform_step   },
	/* Component specific modules */
	{ DSD_555_ASTBL   ,"DSD_555_ASTBL"   ,sizeof(struct dsd_555_astbl_context)   ,dsd_555_astbl_reset   ,dsd_555_astbl_step   },
	{ DSD_555_MSTBL   ,"DSD_555_MSTBL"   ,sizeof(struct dsd_555_mstbl_context)   ,dsd_555_mstbl_reset   ,dsd_555_mstbl_step   },
	{ DSD_555_CC      ,"DSD_555_CC"      ,sizeof(struct dsd_555_cc_context)      ,dsd_555_cc_reset      ,dsd_555_cc_step      },
	{ DSD_566         ,"DSD_566"         ,sizeof(struct dsd_566_context)         ,dsd_566_reset         ,dsd_566_step         },

	/* must be the last one */
	{ DSS_NULL        ,"DSS_NULL"        ,0                                      ,NULL                  ,NULL                 }
};



/*************************************
 *
 *	Find a given node
 *
 *************************************/

struct node_description *discrete_find_node(void *chip, int node)
{
	struct discrete_info *info = chip ? chip : discrete_current_context;
	if (node < NODE_START || node > NODE_END) return NULL;
	return info->indexed_node[node - NODE_START];
}



/*************************************
 *
 *	Master discrete system start
 *
 *************************************/

static void *discrete_start(int sndindex, int clock, const void *config)
{
	struct discrete_sound_block *intf = (struct discrete_sound_block *)config;
	struct discrete_info *info;

	info = auto_malloc(sizeof(*info));
	memset(info, 0, sizeof(*info));

	/* do nothing if sound is disabled */
	if (!Machine->sample_rate)
		return info;

	/* create the logfile */
	if (DISCRETE_DEBUGLOG && !disclogfile)
		disclogfile = fopen("discrete.log", "w");

	/* first pass through the nodes: sanity check, fill in the indexed_nodes, and make a total count */
	discrete_log("discrete_sh_start() - Doing node list sanity check");
	for (info->node_count = 0; intf[info->node_count].type != DSS_NULL; info->node_count++)
	{
		/* make sure we don't have too many nodes overall */
		if (info->node_count > DISCRETE_MAX_NODES)
			osd_die("discrete_sh_start() - Upper limit of %d nodes exceeded, have you terminated the interface block.", DISCRETE_MAX_NODES);

		/* make sure the node number is in range */
		if (intf[info->node_count].node < NODE_START || intf[info->node_count].node > NODE_END)
			osd_die("discrete_sh_start() - Invalid node number on node %02d descriptor\n", info->node_count);

		/* make sure the node type is valid */
		if (intf[info->node_count].type > DSO_OUTPUT)
			osd_die("discrete_sh_start() - Invalid function type on NODE_%02d\n", intf[info->node_count].node - NODE_START);
	}
	info->node_count++;
	discrete_log("discrete_sh_start() - Sanity check counted %d nodes", info->node_count);

	/* allocate memory for the array of actual nodes */
	info->node_list = auto_malloc(info->node_count * sizeof(info->node_list[0]));
	if (!info->node_list)
		osd_die("discrete_sh_start() - Out of memory allocating info->node_list\n");
	memset(info->node_list, 0, info->node_count * sizeof(info->node_list[0]));

	/* allocate memory for the node execution order array */
	info->running_order = auto_malloc(info->node_count * sizeof(info->running_order[0]));
	if (!info->running_order)
		osd_die("discrete_sh_start() - Out of memory allocating info->running_order\n");
	memset(info->running_order, 0, info->node_count * sizeof(info->running_order[0]));

	/* allocate memory to hold pointers to nodes by index */
	info->indexed_node = auto_malloc(DISCRETE_MAX_NODES * sizeof(info->indexed_node[0]));
	if (!info->indexed_node)
		osd_die("discrete_sh_start() - Out of memory allocating indexed_node\n");
	memset(info->indexed_node, 0, DISCRETE_MAX_NODES * sizeof(info->indexed_node[0]));

	/* initialize the node data */
	init_nodes(info, intf);

	/* now go back and find pointers to all input nodes */
	find_input_nodes(info, intf);

	/* then set up the output nodes */
	setup_output_nodes(info);

	/* reset the system, which in turn resets all the nodes and steps them forward one */
	discrete_reset(info);
	return info;
}



/*************************************
 *
 *	Master discrete system stop
 *
 *************************************/

static void discrete_stop(void *chip)
{
	struct discrete_info *info = chip;
	if (DISCRETE_WAVELOG)
	{
		int outputnum;

		/* close any wave files */
		for (outputnum = 0; outputnum < info->discrete_outputs; outputnum++)
			if (disc_wav_file[outputnum])
				wav_close(disc_wav_file[outputnum]);
	}

	if (DISCRETE_DEBUGLOG)
	{
		/* close the debug log */
	    if (disclogfile)
	    	fclose(disclogfile);
		disclogfile = NULL;
	}
}



/*************************************
 *
 *	Master reset of all nodes
 *
 *************************************/

static void discrete_reset(void *chip)
{
	struct discrete_info *info = chip;
	int nodenum;

	discrete_current_context = info;

	/* loop over all nodes */
	for (nodenum = 0; nodenum < info->node_count; nodenum++)
	{
		struct node_description *node = info->running_order[nodenum];

		/* if the node has a reset function, call it */
		if (node->module.reset)
			(*node->module.reset)(node);

		/* otherwise, just step it */
		else if (node->module.step)
			(*node->module.step)(node);
	}

	discrete_current_context = NULL;
}



/*************************************
 *
 *	Stream update functions
 *
 *************************************/

static void discrete_stream_update(void *param, stream_sample_t **inputs, stream_sample_t **buffer, int length)
{
	struct discrete_info *info = param;
	int samplenum, nodenum, outputnum;

	discrete_current_context = info;

	/* Now we must do length iterations of the node list, one output for each step */
	for (samplenum = 0; samplenum < length; samplenum++)
	{
		/* Setup any input streams */
		for (nodenum = 0; nodenum < info->discrete_input_streams; nodenum++)
		{
			*info->input_stream_data[nodenum] = inputs[nodenum][samplenum];
		}

		/* loop over all nodes */
		for (nodenum = 0; nodenum < info->node_count; nodenum++)
		{
			struct node_description *node = info->running_order[nodenum];

			/* Now step the node */
			if (node->module.step)
				(*node->module.step)(node);
		}

		/* Now put the output into the buffers */
		for (outputnum = 0; outputnum < info->discrete_outputs; outputnum++)
		{
			double val = *info->output_node[outputnum]->input[0];
			buffer[outputnum][samplenum] = (val < -32768) ? -32768 : (val > 32767) ? 32767 : val;
		}
	}

	discrete_current_context = NULL;

	/* add each stream to the logging wavfile */
	if (DISCRETE_WAVELOG)
	{
		if (sizeof(stream_sample_t) == 2)
			for (outputnum = 0; outputnum < info->discrete_outputs; outputnum++)
				wav_add_data_16(disc_wav_file[outputnum], (INT16 *)buffer[outputnum], length);
		else
			for (outputnum = 0; outputnum < info->discrete_outputs; outputnum++)
				wav_add_data_32(disc_wav_file[outputnum], (INT32 *)buffer[outputnum], length, 0);
	}
}



/*************************************
 *
 *	First pass init of nodes
 *
 *************************************/

static void init_nodes(struct discrete_info *info, struct discrete_sound_block *block_list)
{
	int nodenum;

	/* start with no outputs or input streams */
	info->discrete_outputs = 0;
	info->discrete_input_streams = 0;

	/* loop over all nodes */
	for (nodenum = 0; nodenum < info->node_count; nodenum++)
	{
		struct discrete_sound_block *block = &block_list[nodenum];
		struct node_description *node = &info->node_list[nodenum];
		int inputnum, modulenum;

		/* our running order just follows the order specified */
		info->running_order[nodenum] = node;

		/* if we are an output node, track that */
		if (block->node == NODE_OP)
		{
			if (info->discrete_outputs == DISCRETE_MAX_OUTPUTS)
				osd_die("init_nodes() - There can not be more then %d output nodes", DISCRETE_MAX_OUTPUTS);
			info->output_node[info->discrete_outputs++] = node;
		}

		/* otherwise, make sure we are not a duplicate, and put ourselves into the indexed list */
		else
		{
			if (info->indexed_node[block->node - NODE_START])
				osd_die("init_nodes() - Duplicate entries for NODE_%02d\n", block->node - NODE_START);
			info->indexed_node[block->node - NODE_START] = node;
		}

		/* find the requested module */
		for (modulenum = 0; module_list[modulenum].type != DSS_NULL; modulenum++)
			if (module_list[modulenum].type == block->type)
				break;
		if (module_list[modulenum].type != block->type)
			osd_die("init_nodes() - Unable to find discrete module type %d for NODE_%02d\n", block->type, block->node - NODE_START);

		/* static inits */
		node->node = block->node;
		node->module = module_list[modulenum];
		node->output = 0.0;

		node->active_inputs = block->active_inputs;
		for (inputnum = 0; inputnum < DISCRETE_MAX_INPUTS; inputnum++)
		{
			node->input[inputnum] = &(block->initial[inputnum]);
		}

		node->context = NULL;
		node->name = block->name;
		node->custom = block->custom;

		/* allocate memory if necessary */
		if (node->module.contextsize)
		{
			node->context = auto_malloc(node->module.contextsize);
			if (!node->context)
				osd_die("init_nodes() - Out of memory allocating memory for NODE_%02d\n", node->node - NODE_START);
			memset(node->context, 0, node->module.contextsize);
		}

		/* if we are an stream input node, track that */
		if (block->type == DSS_INPUT_STREAM)
		{
			if (info->discrete_input_streams == DISCRETE_MAX_OUTPUTS)
				osd_die("init_nodes() - There can not be more then %d input stream nodes", DISCRETE_MAX_OUTPUTS);
			info->input_stream_data[info->discrete_input_streams++] = node->context;
		}
	}

	/* if no outputs, give an error */
	if (info->discrete_outputs == 0)
		osd_die("init_nodes() - Couldn't find an output node");
}



/*************************************
 *
 *	Find and attach all input nodes
 *
 *************************************/

static void find_input_nodes(struct discrete_info *info, struct discrete_sound_block *block_list)
{
	int nodenum, inputnum;

	/* loop over all nodes */
	for (nodenum = 0; nodenum < info->node_count; nodenum++)
	{
		struct node_description *node = &info->node_list[nodenum];
		struct discrete_sound_block *block = &block_list[nodenum];

		/* loop over all active inputs */
		for (inputnum = 0; inputnum < node->active_inputs; inputnum++)
		{
			int inputnode = block->input_node[inputnum];

			/* if this input is node-based, find the node in the indexed list */
			if ((inputnode > NODE_START) && (inputnode <= NODE_END))
			{
				struct node_description *node_ref = info->indexed_node[inputnode - NODE_START];
				if (!node_ref)
					osd_die("discrete_sh_start - Node NODE_%02d referenced a non existent node NODE_%02d\n", node->node - NODE_START, inputnode - NODE_START);

				node->input[inputnum] = &(node_ref->output);	// Link referenced node out to input
				node->input_is_node |= 1 << inputnum;			// Bit flag if input is node
			}
		}
	}
}



/*************************************
 *
 *	Set up the output nodes
 *
 *************************************/

static void setup_output_nodes(struct discrete_info *info)
{
	int outputnum;

	/* loop over output nodes */
	for (outputnum = 0; outputnum < info->discrete_outputs; outputnum++)
	{
		/* create a logging file */
		if (DISCRETE_WAVELOG)
		{
			char name[32];
			sprintf(name, "discrete%d.wav", outputnum);
			disc_wav_file[outputnum] = wav_open(name, Machine->sample_rate, 1);
		}
	}

	/* initialize the stream(s) */
	info->discrete_stream = stream_create(info->discrete_input_streams, info->discrete_outputs, Machine->sample_rate, info, discrete_stream_update);
}



/**************************************************************************
 * Generic get_info
 **************************************************************************/

static void discrete_set_info(void *token, UINT32 state, union sndinfo *info)
{
	switch (state)
	{
		/* no parameters to set */
	}
}


void discrete_get_info(void *token, UINT32 state, union sndinfo *info)
{
	switch (state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */

		/* --- the following bits of info are returned as pointers to data or functions --- */
		case SNDINFO_PTR_SET_INFO:						info->set_info = discrete_set_info;		break;
		case SNDINFO_PTR_START:							info->start = discrete_start;			break;
		case SNDINFO_PTR_STOP:							info->stop = discrete_stop;				break;
		case SNDINFO_PTR_RESET:							info->reset = discrete_reset;			break;

		/* --- the following bits of info are returned as NULL-terminated strings --- */
		case SNDINFO_STR_NAME:							info->s = "Discrete";					break;
		case SNDINFO_STR_CORE_FAMILY:					info->s = "Analog";						break;
		case SNDINFO_STR_CORE_VERSION:					info->s = "1.0";						break;
		case SNDINFO_STR_CORE_FILE:						info->s = __FILE__;						break;
		case SNDINFO_STR_CORE_CREDITS:					info->s = "Copyright (c) 2004, The MAME Team"; break;
	}
}

