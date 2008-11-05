/******************************************************************************
  This file contains routines to initialize and implement the
  parser-based 'Expr2' model for 3dNLfim.

  Usage:
   * signal model name is 'Expr2'

   * set environment AFNI_NLFIM_EXPR2 to an 'expression'
     (see the 3DNLfim command line example below for one way to do this)

   * 'expression' should use exactly 3 variables (letters):
     + 't' is reserved for the time dimension;
     + any other 2 letters for the free variables
         (i.e., the values to be found by 3dNLfim);
     + Example: 'a*sin(t/b)'

   * You will have to set the variable ranges to values that make sense for
     your problem, as in 3dNLfim options
       -sconstr 0 1 9 -sconstr 1 1 5
     to restrain the first (alphabetically) variable to be between 0 and 9,
     and to make the second (alphabetically) variable be between 1 and 5.
     The default constraints are between 0 and 1, which are probably useless.

   * This model will be SLOW, since the parser evaluation will be sluggish
     compared to optimized and compiled C code.  But it should be useful
     for quick-and-dirty jobs, where you can't be bothered to write a C model.

   * Example:
       1deval -expr '3.5*sin(t/1.5)+gran(0,.1)' -num 100 > q.1D
       3dNLfim -DAFNI_NLFIM_EXPR2='a*sin(t/b)'                    \
               -input q.1D\' -noise Zero -signal Expr2            \
               -sconstr 0 1 9 -sconstr 1 1 5 -bucket 0 qqq -BOTH
     Output (file qqq.1D) gives the estimated parameters as a=3.47268 b=1.50005
     Note input of a 1D file (with default TR=1) using the \' notation.
*******************************************************************************/


/*---------------------------------------------------------------------------*/

#include <math.h>
#include "NLfit_model.h"

void signal_model
(
  float * gs,                /* parameters for signal model */
  int ts_length,             /* length of time series data */
  float ** x_array,          /* independent variable matrix */
  float * ts_array           /* estimated signal model time series */
);


/*---------------------------------------------------------------------------*/
/*
  Routine to initialize the signal model by defining the number of parameters
  in the signal model, the name of the signal model, and the default values
  for the minimum and maximum parameter constraints.
*/

DEFINE_MODEL_PROTOTYPE

MODEL_interface * initialize_model ()
{
  MODEL_interface * mi = NULL;


  /*----- allocate memory space for model interface -----*/
  mi = (MODEL_interface *) XtMalloc (sizeof(MODEL_interface));


  /*----- define interface for the differential - exponential model -----*/

  /*----- name of this model -----*/
  strcpy (mi->label, "Expr2");

  /*----- this is a signal model -----*/
  mi->model_type = MODEL_SIGNAL_TYPE;

  /*----- number of parameters in the model -----*/
  mi->params = 2;

  /*----- parameter labels -----*/
  strcpy (mi->plabel[0], "a");
  strcpy (mi->plabel[1], "b");

  /*----- minimum and maximum parameter constraints -----*/
  mi->min_constr[0] =   0.00;   mi->max_constr[0] =    1.00;
  mi->min_constr[1] =   0.00;   mi->max_constr[1] =    1.00;

  /*----- function which implements the model -----*/
  mi->call_func = &signal_model;


  /*----- return pointer to the model interface -----*/
  return (mi);
}


/*---------------------------------------------------------------------------*/
/*
  Routine to calculate the time series which results from using the
  an exponential signal model with the specified
  model parameters.
*/

#include "parser.h"

void signal_model
(
  float * gs,                /* parameters for signal model */
  int ts_length,             /* length of time series data */
  float ** x_array,          /* independent variable matrix */
  float * ts_array           /* estimated signal model time series */
)

{
  int ii , kk ;

  static char *expr=NULL ;
  static PARSER_code *pcode ;
  static int ia , ib , vlen=0 ;
  static double *atoz[26] , *temp ;

ENTRY("model_expr2") ;

  if( expr == NULL ){
    int qvar ; char sym[4] ;
    expr = getenv("AFNI_NLFIM_EXPR2") ;
    if( expr == NULL )
      ERROR_exit("Can't find AFNI_NLFIM_EXPR2 in environment!") ;
    INFO_message("AFNI_NLFIM_EXPR2 expression = '%s'",expr) ;
    pcode = PARSER_generate_code( expr ) ;
    if( pcode == NULL )
      ERROR_exit("AFNI_NLFIM_EXPR2 contains un-parse-able expression!") ;
    sym[0] = 'T' ; sym[1] = '\0' ;
    if( !PARSER_has_symbol(sym,pcode) )
      ERROR_exit("AFNI_NLFIM_EXPR2 expression doesn't contain 't' (time) symbol!") ;
    for( qvar=ii=0 ; ii < 26 ; ii++ ){
      sym[0] = 'A' + ii ; sym[1] = '\0' ; if( sym[0] == 'T' ) continue ;
      if( PARSER_has_symbol(sym,pcode) ){
        qvar++ ; if( qvar == 1 ) ia = ii ; else if( qvar == 2 ) ib = ii ;
      }
    }
    if( qvar != 2 )
      ERROR_exit("AFNI_NLFIM_EXPR2 expression has %d free variables: should be 2",qvar) ;
    else
      INFO_message("AFNI_NLFIM_EXPR2: variable #1=%c  variable #2=%c",'A'+ia,'A'+ib) ;
    for( ii=0 ; ii < 26 ; ii++ ) atoz[ii] = NULL ;
    temp = NULL ;
  }

  if( ts_length > vlen ){
    vlen = ts_length ;
    for( ii=0 ; ii < 26 ; ii++ ) atoz[ii] = (double *)realloc(atoz[ii],sizeof(double)*vlen) ;
    temp = (double *)realloc(temp,sizeof(double)*vlen) ;
  }

  for( ii=0 ; ii < ts_length ; ii++ ){
    atoz[ia][ii] = (double)gs[0] ;
    atoz[ib][ii] = (double)gs[1] ;
    atoz[19][ii] = (double)x_array[ii][1] ;
  }

  PARSER_evaluate_vector( pcode , atoz , ts_length , temp ) ;

  for( ii=0 ; ii < ts_length ; ii++ )
    ts_array[ii] = (float)temp[ii] ;

  EXRETURN ;
}
