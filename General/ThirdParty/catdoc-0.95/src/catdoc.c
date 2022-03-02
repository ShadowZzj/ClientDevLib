/*
  Copyright 1996-2003 Victor Wagner
  Copyright 2003 Alex Ott
  This file is released under the GPL.  Details can be
  found in the file COPYING accompanying this distribution.
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include "catdoc.h"

static void help(void);

#define PATH_MAX 1024

// char szKempDataDir[PATH_MAX+1];


int signature_check = 1;
int forced_charset = 0; /* Flag which disallow rtf parser override charset*/
int wrap_margin = WRAP_MARGIN;
int (*get_unicode_char)(FILE *f,long *offset,long fileend) =NULL;

#ifdef __WATCOMC__
/* watcom doesn't provide way to access program args via global variable */
/* so we would hack it ourselves in Borland-compatible way*/
char **_argv;
int _argc;
#endif
/**************************************************************/
/*       Main program                                         */
/*  Processes options, reads charsets  files and substitution */
/*  maps and passes all remaining args to processfile         */
/**************************************************************/
int doc_main_unused(int argc, char **argv, const char* kepmDataDir)  {
	FILE *f;
	int c,i;
	char *tempname;
	short int *tmp_charset;
	int stdin_processed=0;
#ifdef __WATCOMC__
	_argv=argv;
	_argc=argc;
#endif
	read_config_file(SYSTEMRC);
    
    //sprintf(szKempDataDir, "%s", kepmDataDir);
    
    charset_path = kepmDataDir;
    map_path = kepmDataDir;
    
#ifdef USERRC
	tempname=find_file(strdup(USERRC),getenv("HOME"));
	if (tempname) {
		read_config_file(tempname);
		free(tempname);
	}
#endif
#ifdef HAVE_LANGINFO
	get_locale_charset();
#endif	
//	while ((c=getopt(argc,argv,"Vls:d:f:taubxv8wm:"))!=-1) {
//		switch (c) {
//			case 's':
//				check_charset(&source_csname,optarg);
//				forced_charset = 1;
//				break;
//			case 'd':
//				check_charset(&dest_csname,optarg);
//				break;
//			case 'f':
//				format_name=strdup(optarg);
//				break;
//			case 't':
//				format_name=strdup("tex");
//				break;
//			case 'a':
//				format_name=strdup("ascii");
//				break;
//			case 'u':
//				get_unicode_char = get_word8_char;
//				break;
//			case '8':
//				get_unicode_char = get_8bit_char;
//				break;
//			case 'v':
//				verbose=1;
//				break;
//			case 'w':
//				wrap_margin=0; /* No wrap */
//				break;
//			case 'm': {
//						  char *endptr;
//						  wrap_margin = (int)strtol(optarg,&endptr,0);
//						  if (*endptr) {
//							  fprintf(stderr,"Invalid wrap margin value `%s'\n",optarg);
//							  exit(1);
//						  }
//						  break;
//					  }
//			case 'l': list_charsets(); exit(0);	     
//			case 'b': signature_check =0; break;
//			case 'x': unknown_as_hex = 1; break;
//			case 'V': printf("Catdoc Version %s\n",CATDOC_VERSION);
//					  exit(0);
//			default:
//					  help();
//					  exit(1);
//		}
//	}

	source_charset = read_charset(source_csname);
	if (!source_charset) exit(1);
	if (strncmp(dest_csname,"utf-8",6)) {
		tmp_charset = read_charset(dest_csname);
		if (!tmp_charset) exit(1);
		target_charset= make_reverse_map(tmp_charset);
		free(tmp_charset);
	} else {
		target_charset = NULL;
	}  
	spec_chars=read_substmap(stradd(format_name,SPEC_EXT));
	if (!spec_chars) {
		fprintf(stderr,"Cannot read substitution map %s%s\n",format_name,
				SPEC_EXT);
		exit(1);
	}  
	replacements=read_substmap(stradd(format_name,REPL_EXT));
	if (!replacements) {
		fprintf(stderr,"Cannot read replacement map %s%s\n",format_name,
				REPL_EXT);
		exit(1);
	}  

	if (LINE_BUF_SIZE-longest_sequence<=wrap_margin) {
		fprintf(stderr,"wrap margin is too large. cannot proceed\n");
		exit(1);
	}  

	set_std_func();
//	if (optind == argc) {
//		if (isatty(fileno(stdin))) {
//			help();
//			exit(0);
//		}
//		if (input_buffer) setvbuf(stdin,input_buffer,_IOFBF,FILE_BUFFER);
//		return analyze_format(stdin);
//	}
	c=0;
	for (i=1;i<argc;i++) {
		if (!strcmp(argv[i],"-")) {
			if (stdin_processed) {
				fprintf(stderr,"Cannot process stdin twice\n");
				exit(1);
			}

			analyze_format(stdin);
			stdin_processed=1;
		} else {
			f=fopen(argv[i],"rb");
			if (!f) {
				c=1;
				perror("catdoc");
				continue;
			}

			c=analyze_format(f);
			fclose(f);
		}
	}
	return c;
}
/************************************************************************/
/* Displays  help message                                               */
/************************************************************************/
static void help (void) {
	printf("Usage:\n catdoc [-vu8btawxlV] [-m number] [-s charset] "
			"[-d charset] [ -f format] files\n");
}
