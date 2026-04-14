#!/bin/tcsh

# ============================================================
# Condor wrapper for TeBATDrift
#
# Arguments:
#   1 = config file
#   2 = start step
#   3 = end step
#   4 = output file
# ============================================================

set SCRIPT_DIR = `dirname $0`
set PROJECT_ROOT = `cd ${SCRIPT_DIR}/.. && pwd`

source ${PROJECT_ROOT}/setup_env.csh

set CONFIG    = $1
set STARTSTEP = $2
set ENDSTEP   = $3
set OUTFILE   = $4

echo "Running TeBATDrift Condor job"
echo "  PROJECT_ROOT = ${PROJECT_ROOT}"
echo "  CONFIG       = ${CONFIG}"
echo "  STARTSTEP    = ${STARTSTEP}"
echo "  ENDSTEP      = ${ENDSTEP}"
echo "  OUTFILE      = ${OUTFILE}"

${PROJECT_ROOT}/build/GarfieldDriftRes ${CONFIG} --start-step ${STARTSTEP} --end-step ${ENDSTEP} --output ${OUTFILE}
set EXITCODE = $status

echo "GarfieldDriftRes exited with code ${EXITCODE}"
exit ${EXITCODE}
