/****************************************************************************\
 *  opts.c - sinfo command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Moe Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/common/xstring.h"
#include "src/sinfo/print.h"
#include "src/sinfo/sinfo.h"

/* FUNCTIONS */
static List  _build_state_list( char* str );
static List  _build_all_states_list( void );
static char *_get_prefix(char *token);
static void  _help( void );
static int   _parse_format( char* );
static int   _parse_state(char *str, uint16_t *states);
static void  _parse_token( char *token, char *field, int *field_size, 
                           bool *right_justify, char **suffix);
static void  _print_options( void );
static void  _print_version( void );
static void  _usage( void );

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char *argv[])
{
	char *env_val = NULL;
	int opt_char;
	int option_index;
	static struct option long_options[] = {
		{"exact",     no_argument,       0, 'e'},
		{"noheader",  no_argument,       0, 'h'},
		{"iterate",   required_argument, 0, 'i'},
		{"long",      no_argument,       0, 'l'},
		{"nodes",     required_argument, 0, 'n'},
		{"Node",      no_argument,       0, 'N'},
		{"format",    required_argument, 0, 'o'},
		{"partition", required_argument, 0, 'p'},
		{"summarize", no_argument,       0, 's'},
		{"sort",      required_argument, 0, 'S'},
		{"states",    required_argument, 0, 't'},
		{"verbose",   no_argument,       0, 'v'},
		{"version",   no_argument,       0, 'V'},
		{"help",      no_argument,       0, '1'},
		{"usage",     no_argument,       0, '2'}
	};

	while((opt_char = getopt_long(argc, argv, "ehi:ln:No:p:sS:t:vV12",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
			case (int)'?':
				fprintf(stderr, "Try \"sinfo --help\" for more information\n");
				exit(1);
				break;
			case (int)'e':
				params.exact_match = true;
				break;
			case (int)'h':
				params.no_header = true;
				break;
			case (int) 'i':
				params.iterate= atoi(optarg);
				if (params.iterate <= 0) {
					fprintf(stderr, 
						"Error: --iterate=%s",
						optarg);
					exit(1);
				}
				break;
			case (int) 'l':
				params.long_output = true;
				break;
			case (int) 'n':
				params.nodes= xstrdup(optarg);
				break;
			case (int) 'N':
				params.node_flag = true;
				break;
			case (int) 'o':
				params.format = xstrdup(optarg);
				break;
			case (int) 'p':
				params.partition = xstrdup(optarg);
				break;
			case (int) 's':
				params.summarize = true;
				break;
			case (int) 'S':
				params.sort = xstrdup(optarg);
				break;
			case (int) 't':
				params.states = xstrdup(optarg);
				params.state_list = 
					_build_state_list(params.states);
				break;
			case (int) 'v':
				params.verbose++;
				break;
			case (int) 'V':
				_print_version();
				exit(0);
			case (int) '1':
				_help();
				exit(0);
			case (int) '2':
				_usage();
				exit(0);
		}
	}

	if ( ( params.format == NULL ) && 
	     ( env_val = getenv("SINFO_FORMAT") ) )
		params.format = xstrdup(env_val);

	if ( ( params.partition == NULL ) && 
	     ( env_val = getenv("SINFO_PARTITION") ) )
		params.partition = xstrdup(env_val);

	if ( ( params.partition == NULL ) && 
	     ( env_val = getenv("SINFO_SORT") ) )
		params.sort = xstrdup(env_val);

	if ( params.format == NULL ) {
		if ( params.summarize ) 
			params.format = "%9P %.5a %.9l %.15F  %N";
		else if ( params.node_flag ) {
			params.node_field_flag = true;	/* compute size later */
			if ( params.long_output ) {
				params.format = "%N %.5D %.9P %.11T %.4c "
					        "%.6m %.8d %.6w %.8f %20R";
			} else {
				params.format = "%N %.5D %.9P %6t";
			}
		} else {
			if ( params.long_output )
				params.format = "%9P %.5a %.9l %.8s %.4r %.5h "
					        "%.10g %.5D %.11T %N";
			else
				params.format = "%9P %.5a %.9l %.5D %.6t %N";
		}
	}
	_parse_format( params.format );

	if (params.nodes || params.partition || params.state_list)
		params.filtering = true;

	if (params.verbose)
		_print_options();
}

/*
 * _build_state_list - build a list of job states
 * IN str - comma separated list of job states
 * RET List of enum job_states values
 */
static List 
_build_state_list( char* str )
{
	List my_list;
	char *state, *tmp_char, *my_state_list;
	uint16_t *state_id;

	if ( str == NULL)
		return NULL;
	if ( strcasecmp( str, "all" ) == 0 )
		return _build_all_states_list ();

	my_list = list_create( NULL );
	my_state_list = xstrdup( str );
	state = strtok_r( my_state_list, ",", &tmp_char );
	while (state) 
	{
		state_id = xmalloc( sizeof( uint16_t ) );
		if ( _parse_state( state, state_id ) != SLURM_SUCCESS )
			exit( 1 );
		list_append( my_list, state_id );
		state = strtok_r( NULL, ",", &tmp_char );
	}
	xfree( my_state_list );
	return my_list;
}

/*
 * _build_all_states_list - build a list containing all possible job states
 * RET List of enum job_states values
 */
static List 
_build_all_states_list( void )
{
	List my_list;
	int i;
	uint16_t *state_id;

	my_list = list_create( NULL );
	for (i = 0; i<NODE_STATE_END; i++) {
		state_id = xmalloc( sizeof( uint16_t ) );
		*state_id = (uint16_t) i;
		list_append( my_list, state_id );
	}
	return my_list;
}

/*
 * _parse_state - convert node state name string to numeric value
 * IN str - state name
 * OUT states - node_state value corresponding to str
 * RET 0 or error code
 */
static int
_parse_state( char* str, uint16_t* states )
{	
	int i;
	char *state_names;

	for (i = 0; i<NODE_STATE_END; i++) {
		if (strcasecmp (node_state_string(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
		if (strcasecmp (node_state_string_compact(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
	}

	fprintf (stderr, "Invalid node state specified: %s\n", str);
	state_names = xstrdup(node_state_string(0));
	for (i=1; i<NODE_STATE_END; i++) {
		xstrcat(state_names, ",");
		xstrcat(state_names, node_state_string(i));
	}
	fprintf (stderr, "Valid node states include: %s\n", state_names);
	xfree (state_names);
	return SLURM_ERROR;
}

/* Take the user's format specification and use it to build build the 
 *	format specifications (internalize it to print.c data structures) */
static int 
_parse_format( char* format )
{
	int field_size;
	bool right_justify;
	char *prefix, *suffix, *token, *tmp_char, *tmp_format;
	char field[1];

	if (format == NULL) {
		fprintf( stderr, "Format option lacks specification\n" );
		exit( 1 );
	}

	params.format_list = list_create( NULL );
	if ((prefix = _get_prefix(format)))
		format_add_prefix( params.format_list, 0, 0, prefix); 

	tmp_format = xstrdup( format );
	token = strtok_r( tmp_format, "%", &tmp_char);
	if (token && (format[0] != '%'))	/* toss header */
		token = strtok_r( NULL, "%", &tmp_char );
	while (token) {
		_parse_token( token, field, &field_size, &right_justify, 
			      &suffix);
		if        (field[0] == 'a') {
			params.match_flags.avail_flag = true;
			format_add_avail( params.format_list,
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'A') {
			format_add_nodes_ai( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'c') {
			format_add_cpus( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'd') {
			format_add_disk( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'D') {
			format_add_nodes( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'f') {
			params.match_flags.features_flag = true;
			format_add_features( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'F') {
			format_add_nodes_aiot( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'g') {
			params.match_flags.groups_flag = true;
			format_add_groups( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'h') {
			params.match_flags.share_flag = true;
			format_add_share( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'l') {
			params.match_flags.max_time_flag = true;
			format_add_time( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'm') {
			format_add_memory( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'N') {
			format_add_node_list( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'P') {
			params.match_flags.partition_flag = true;
			format_add_partition( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'r') {
			params.match_flags.root_flag = true;
			format_add_root( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'R') {
			params.match_flags.reason_flag = true;
			format_add_reason( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 's') {
			params.match_flags.job_size_flag = true;
			format_add_size( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 't') {
			params.match_flags.state_flag = true;
			format_add_state_compact( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'T') {
			params.match_flags.state_flag = true;
			format_add_state_long( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'w') {
			format_add_weight( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else
			fprintf(stderr, "Invalid node format specification: %c\n", 
			        field[0] );
		token = strtok_r( NULL, "%", &tmp_char);
	}

	xfree( tmp_format );
	return SLURM_SUCCESS;
}

/* Take a format specification and copy out it's prefix
 * IN/OUT token - input specification, everything before "%" is removed
 * RET - everything before "%" in the token
 */
static char *
_get_prefix( char *token )
{
	char *pos, *prefix;

	if (token == NULL) 
		return NULL;

	pos = strchr(token, (int) '%');
	if (pos == NULL)	/* everything is prefix */
		return xstrdup(token);
	if (pos == token)	/* no prefix */
		return NULL;

	pos[0] = '\0';		/* some prefix */
	prefix = xstrdup(token);
	pos[0] = '%';
	memmove(token, pos, (strlen(pos)+1));
	return prefix;
}

/* Take a format specification and break it into its components
 * IN token - input specification without leading "%", eg. ".5u"
 * OUT field - the letter code for the data type
 * OUT field_size - byte count
 * OUT right_justify - true of field to be right justified
 * OUT suffix - tring containing everthing after the field specification
 */
static void
_parse_token( char *token, char *field, int *field_size, bool *right_justify, 
	      char **suffix)
{
	int i = 0;

	assert (token != NULL);

	if (token[i] == '.') {
		*right_justify = true;
		i++;
	} else
		*right_justify = false;

	*field_size = 0;
	while ((token[i] >= '0') && (token[i] <= '9'))
		*field_size = (*field_size * 10) + (token[i++] - '0');

	field[0] = token[i++];

	*suffix = xstrdup(&token[i]);
}

/* print the parameters specified */
void _print_options( void )
{
	printf("-----------------------------\n");
	printf("exact       = %d\n", params.exact_match);
	printf("filtering   = %s\n", params.filtering ? "true" : "false");
	printf("format      = %s\n", params.format);
	printf("iterate     = %d\n", params.iterate );
	printf("long        = %s\n", params.long_output ? "true" : "false");
	printf("no_header   = %s\n", params.no_header   ? "true" : "false");
	printf("node_field  = %s\n", params.node_field_flag ? 
					"true" : "false");
	printf("node_format = %s\n", params.node_flag   ? "true" : "false");
	printf("nodes       = %s\n", params.nodes ? params.nodes : "n/a");
	printf("partition   = %s\n", params.partition ? 
					params.partition: "n/a");
	printf("states      = %s\n", params.states);
	printf("sort        = %s\n", params.sort);
	printf("summarize   = %s\n", params.summarize   ? "true" : "false");
	printf("verbose     = %d\n", params.verbose);
	printf("-----------------------------\n");
	printf("avail_flag      = %s\n", params.match_flags.avail_flag ?
			"true" : "false");
	printf("features_flag   = %s\n", params.match_flags.features_flag ?
			"true" : "false");
	printf("groups_flag     = %s\n", params.match_flags.groups_flag ?
					"true" : "false");
	printf("job_size_flag   = %s\n", params.match_flags.job_size_flag ?
					"true" : "false");
	printf("max_time_flag   = %s\n", params.match_flags.max_time_flag ?
					"true" : "false");
	printf("partition_flag  = %s\n", params.match_flags.partition_flag ?
			"true" : "false");
	printf("reason_flag     = %s\n", params.match_flags.reason_flag ?
			"true" : "false");
	printf("root_flag       = %s\n", params.match_flags.root_flag ?
			"true" : "false");
	printf("share_flag      = %s\n", params.match_flags.share_flag ?
			"true" : "false");
	printf("state_flag      = %s\n", params.match_flags.state_flag ?
			"true" : "false");
	printf("-----------------------------\n\n");
}


static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _usage( void )
{
	printf("Usage: sinfo [-i seconds] [-t node_state] [-p PARTITION] [-n NODES]\n");
	printf("            [-S fields] [-o format] [--usage] [-elNsv]\n");
}

static void _help( void )
{
	printf("Usage: sinfo [options]\n");
	printf("  -e, --exact                   group nodes only on exact match of\n");
	printf("                                configuration\n");
	printf("  -h, --noheader                no headers on output\n");
	printf("  -i, --iterate=seconds         specify an interation period\n");
	printf("  -l, --long                    long output - displays more information\n");
	printf("  -n, --nodes=NODES             report on specific node(s)\n");
	printf("  -N, --Node                    Node-centric format\n");
	printf("  -o, --format=format           format specification\n");
	printf("  -p, --partition=PARTITION     report on specific partition\n");
	printf("  -s, --summarize               report state summary only\n");
	printf("  -S, --sort=fields             comma seperated list of fields to sort on\n");
	printf("  -t, --states=node_state       specify the what states of nodes to view\n");
	printf("  -v, --verbose                 verbosity level\n");
	printf("  -V, --version                 output version information and exit\n");
	printf("\nHelp options:\n");
	printf("  --help                        show this help message\n");
	printf("  --usage                       display brief usage message\n");
}
