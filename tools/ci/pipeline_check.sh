#!/bin/bash
# Copyright (c) 2022, XMOS Ltd, All rights reserved
# This script performs Avona pipeline filtering keyword detection testing.

set -e

# help text
help()
{
   echo "Avona pipeline filtering keyword detection testing"
   echo
   echo "Syntax: pipeline_check.sh [-h] pipeline sample_directory/ sensory_exe_directory/"
   echo
   echo "options:"
   echo "h     Print this Help."
   echo "Valid values for pipeline: ADEC, ALTARCH"
   echo
}

# flag arguments
while getopts h option
do
    case "${option}" in
        h) help
           exit;;
    esac
done

# assign command line args
USER_PIPELINE=${@:$OPTIND:1}
# TODO: add parse to check for and add possible missing trailing / on dirs
SAMPLE_DIR=${@:$OPTIND+1:1}
SENSORY_EXE_DIR=${@:$OPTIND+2:1}
MODEL_DIR="${SENSORY_EXE_DIR}../model/"

# discern repository root
SLN_AVONA_ROOT=`git rev-parse --show-toplevel`
CI_CHECK_DIR="${SLN_AVONA_ROOT}/tools/ci/"
source ${SLN_AVONA_ROOT}/tools/ci/helper_functions.sh

# sensory
SENSORY_MODEL="spot-alexa-rpi-31000.snsr"
SENSORY_EXE="spot-eval_x86_64-pc-linux-gnu"
K_WORD="alexa"

# binaries' directory
BINARY_DIR="${SLN_AVONA_ROOT}/dist/"

# audio filenames, min instances, max instances
ALL_FILES=(
    "clipped_InHouse_XVF3510v080_v1.2_20190423_Loc1_Clean_XMOS_DUT1_80dB_Take1           24      24"
    "clipped_InHouse_XVF3510v080_v1.2_20190423_Loc1_Noise1_65dB_XMOS_DUT1_80dB_Take1     22      24"
    "clipped_InHouse_XVF3510v080_v1.2_20190423_Loc1_Noise2_70dB__Take1                   21      25"
    "clipped_InHouse_XVF3510v080_v1.2_20190423_Loc2_Noise1_65dB__Take1                   24      25"
)

# file extension
SUFFIX_EXTENSION=".wav"

# sox args
# define number of channels
ARG_SOX_CHANNEL="-c"
NUM_CHANNELS=4

# output filename prefix
PREFIX_OUTPUT="output_"

# assign pipeline specific dir for files
P_NAME=${USER_PIPELINE}

# Create a folder to store output files for each AEC config
PIPELINE_DIR="${CI_CHECK_DIR}${P_NAME}/"
mkdir -p ${PIPELINE_DIR}

# fresh logs
RESULTS="${P_NAME}_results.log"
rm -rf ${RESULTS}

echo '************************'
echo '* Checking' ${P_NAME}
echo '* Checking' ${P_NAME} >> ${RESULTS}
echo '************************'

# test the audio
# DEBUG: j = 3, just do the last file
for ((j = 3; j < ${#ALL_FILES[@]}; j += 1)); do
    read -ra FIELDS <<< ${ALL_FILES[j]}
    FILE_NAME=${FIELDS[0]}
    MIN=${FIELDS[1]}
    MAX=${FIELDS[2]}

    # process .wavs with Sox
    SOX_INPUT="${SAMPLE_DIR}${FILE_NAME}${SUFFIX_EXTENSION}"
    SOX_OUTPUT="${PREFIX_OUTPUT}${FILE_NAME}${SUFFIX_EXTENSION}"

    # syntax: script args input_file output_file
    source process_wav.sh ${ARG_SOX_CHANNEL}${NUM_CHANNELS} ${SOX_INPUT} ${P_NAME}/${SOX_OUTPUT}

    # DEBUG: sox remix 1 command not working.
    # sleep 3

    # single out ASR channel
    # mono filename prefix
    PREFIX_MONO="mono_"
    SOX_MONO_OUTPUT="${PREFIX_MONO}${SOX_OUTPUT}"
    # syntax: sox input_file output_file remix num_channels
    sox ${P_NAME}/${SOX_OUTPUT} ${P_NAME}/${SOX_MONO_OUTPUT} remix 1

    # use Sensory to generate logs of keyword detection, and then
    # count keyword occurrences in the logs
    DETECTIONS=$(grep -o -I ${K_WORD} ${P_NAME}/${FILE_NAME}.log | wc -l)
    # syntax: sensory_exe -t model -s operating-point=5 -v input_file
    ${SENSORY_EXE_DIR}${SENSORY_EXE} -t ${MODEL_DIR}${SENSORY_MODEL} -s operating-point=5 -v ${P_NAME}/${SOX_MONO_OUTPUT} 2>&1 | tee ${P_NAME}/${FILE_NAME}.log
    (echo ${FILE_NAME} >> ${RESULTS} && echo "keyword=${K_WORD}, detected=${DETECTIONS}, min=${MIN}, max=${MAX}" >> ${RESULTS})

done # for ((j = 0; j < ${#ALL_FILES[@]}; j += 1)); do

# newline
echo >> ${RESULTS}

# don't keep these files used for sensory
# DEBUG: disabled for testing
# "rm -rf mono_"
