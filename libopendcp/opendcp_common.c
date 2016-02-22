/*
    OpenDCP: Builds Digital Cinema Packages
    Copyright (c) 2010-2013 Terrence Meiczinger, All Rights Reserved

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <sys/stat.h>
#include <dirent.h>
#include <inttypes.h>
#include <ctype.h>
#include <time.h>
#include "opendcp.h"

const char *XML_HEADER  = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>";

const char *NS_CPL[]    = { "none",
                            "http://www.digicine.com/PROTO-ASDCP-CPL-20040511#", /* MXF Interop */
                            "http://www.smpte-ra.org/schemas/429-7/2006/CPL"     /* SMPTE */
                          };

const char *NS_CPL_3D[] = { "none",
                            "http://www.digicine.com/schemas/437-Y/2007/Main-Stereo-Picture-CPL",   /* MXF Interop */
                            "http://www.smpte-ra.org/schemas/429-10/2008/Main-Stereo-Picture-CPL"   /* SMPTE */
                          };

const char *NS_PKL[]    = { "none",
                            "http://www.digicine.com/PROTO-ASDCP-PKL-20040311#", /* MXF Interop */
                            "http://www.smpte-ra.org/schemas/429-8/2007/PKL"     /* SMPTE */
                          };

const char *NS_AM[]     = { "none",
                            "http://www.digicine.com/PROTO-ASDCP-AM-20040311#",  /* MXF Interop */
                            "http://www.smpte-ra.org/schemas/429-9/2007/AM"      /* SMPTE */
                          };

const char *DS_DSIG = "http://www.w3.org/2000/09/xmldsig#";                      /* digial signature */
const char *DS_CMA  = "http://www.w3.org/TR/2001/REC-xml-c14n-20010315";         /* canonicalization method */
const char *DS_DMA  = "http://www.w3.org/2000/09/xmldsig#sha1";                  /* digest method */
const char *DS_TMA  = "http://www.w3.org/2000/09/xmldsig#enveloped-signature";   /* transport method */

const char *DS_SMA[] = { "none",
                         "http://www.w3.org/2000/09/xmldsig#rsa-sha1",           /* MXF Interop */
                         "http://www.w3.org/2001/04/xmldsig-more#rsa-sha256"     /* SMPTE */
                       };

const char *RATING_AGENCY[] = { "none",
                                "http://www.mpaa.org/2003-ratings",
                                "http://rcq.qc.ca/2003-ratings"
                              };

const char *OPENDCP_LOGLEVEL_NAME[] = { "NONE",
                                        "ERROR",
                                        "WARN",
                                        "INFO",
                                        "DEBUG"
                                      };

void dcp_fatal(opendcp_t *opendcp, char *format, ...) {
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    fprintf(stderr, "%s\n", msg);
    va_end(args);
    opendcp_delete(opendcp);
    exit(OPENDCP_ERROR);
}

/**
determine an assets type

This function will return the class type of an asset essence.

@param  asset_t
@return int
*/
int get_asset_type(asset_t asset) {
    switch (asset.essence_type) {
        case AET_MPEG2_VES:
        case AET_JPEG_2000:
        case AET_JPEG_2000_S:
            return ACT_PICTURE;
            break;

        case AET_PCM_24b_48k:
        case AET_PCM_24b_96k:
            return ACT_SOUND;
            break;

        case AET_TIMED_TEXT:
            return ACT_TIMED_TEXT;
            break;

        default:
            return ACT_UNKNOWN;
    }
}

int opendcp_callback_null(void *args) {
    UNUSED(args);

    return OPENDCP_NO_ERROR;
}

/**
create an opendcp context

This function will allocate and initialize an opendcp context.

@param  NONE
@return An initialized opendcp_t structure on success, otherwise returns NULL
*/
opendcp_t *opendcp_create() {
    opendcp_t *opendcp;

    /* allocation opendcp memory */
    opendcp = malloc(sizeof(opendcp_t));

    if (!opendcp) {
        return NULL;
    }

    memset(opendcp, 0, sizeof (opendcp_t));

    /* initialize opendcp */
    opendcp->log_level = LOG_WARN;
    sprintf(opendcp->dcp.issuer, "%.80s %.80s", OPENDCP_NAME, OPENDCP_VERSION);
    sprintf(opendcp->dcp.creator, "%.80s %.80s", OPENDCP_NAME, OPENDCP_VERSION);
    sprintf(opendcp->dcp.annotation, "%.128s", DCP_ANNOTATION);
    sprintf(opendcp->dcp.title, "%.80s", DCP_TITLE);
    sprintf(opendcp->dcp.kind, "%.15s", DCP_KIND);
    generate_timestamp(opendcp->dcp.timestamp);
    opendcp->mxf.write_hmac = 1;

    /* initialize callbacks */
    opendcp->mxf.frame_done.callback  = opendcp_callback_null;
    opendcp->mxf.frame_done.argument  = NULL;
    opendcp->mxf.file_done.callback   = opendcp_callback_null;
    opendcp->mxf.file_done.argument   = NULL;
    opendcp->dcp.sha1_update.callback = opendcp_callback_null;
    opendcp->dcp.sha1_update.argument = NULL;
    opendcp->dcp.sha1_done.callback = opendcp_callback_null;
    opendcp->dcp.sha1_done.argument = NULL;

    return opendcp;
}

/**
delete an opendcp context

This function will de-allocate an opendcp context.

@param  opendcp an opendcp_t structure
@return returns OPENDCP_NO_ERROR on success
*/
int opendcp_delete(opendcp_t *opendcp) {
    if ( opendcp != NULL) {
        free(opendcp);
    }

    return OPENDCP_NO_ERROR;
}

/**
create a pkl and add information

This function populates a pkl data structure with DCP information
from a dcp_t structure.

@param  dcp dcp_t structure
@param  pkl pkl_t  structure
@return NONE
*/
void create_pkl(dcp_t dcp, pkl_t *pkl) {
    char uuid_s[40];

    memset(pkl, 0, sizeof(pkl_t));

    strcpy(pkl->issuer,     dcp.issuer);
    strcpy(pkl->creator,    dcp.creator);
    strcpy(pkl->annotation, dcp.annotation);
    strcpy(pkl->timestamp,  dcp.timestamp);
    pkl->cpl_count = 0;

    /* Generate UUIDs */
    uuid_random(uuid_s);
    sprintf(pkl->uuid, "%.36s", uuid_s);

    /* Generate XML filename */
    if ( !strcmp(dcp.basename, "") ) {
        sprintf(pkl->filename, "PKL_%.40s.xml", pkl->uuid);
    }
    else {
        sprintf(pkl->filename, "PKL_%.40s.xml", dcp.basename);
    }

    return;
}

/**
add packaging list to dcp

This function adds a pkl to a dcp_t structure

@param  dcp dcp_t structure
@param  pkl pkl_t structure
@return NONE
*/
void add_pkl_to_dcp(dcp_t *dcp, pkl_t pkl) {
    memcpy(dcp[dcp->pkl_count].pkl, &pkl, sizeof(pkl_t));
    dcp->pkl_count++;

    return;
}

/**
add content playlist to packaging list

This function populates a cpl data structure with DCP information
from a dcp_t structure.

@param  dcp dcp_t structure
@param  cpl cpl_t structure
@return NONE
*/
void create_cpl(dcp_t dcp, cpl_t *cpl) {
    char uuid_s[40];

    memset(cpl, 0, sizeof(cpl_t));

    strcpy(cpl->annotation, dcp.annotation);
    strcpy(cpl->issuer,     dcp.issuer);
    strcpy(cpl->creator,    dcp.creator);
    strcpy(cpl->title,      dcp.title);
    strcpy(cpl->kind,       dcp.kind);
    strcpy(cpl->rating,     dcp.rating);
    strcpy(cpl->timestamp,  dcp.timestamp);
    cpl->reel_count = 0;

    uuid_random(uuid_s);
    sprintf(cpl->uuid, "%.36s", uuid_s);

    /* Generate XML filename */
    if ( !strcmp(dcp.basename, "") ) {
        sprintf(cpl->filename, "CPL_%.40s.xml", cpl->uuid);
    }
    else {
        sprintf(cpl->filename, "CPL_%.40s.xml", dcp.basename);
    }

    return;
}

/**
add packaging list to packaging list

This functio adds a cpl to a pkl structure

@param  dcp dcp_t structure
@param  cpl cpl_t structure
@return NONE
*/
void add_cpl_to_pkl(pkl_t *pkl, cpl_t cpl) {
    memcpy(&pkl->cpl[pkl->cpl_count], &cpl, sizeof(cpl_t));
    pkl->cpl_count++;

    return;
}

int init_asset(asset_t *asset) {
    memset(asset, 0, sizeof(asset_t));

    return OPENDCP_NO_ERROR;
}

void create_reel(dcp_t dcp, reel_t *reel) {
    char uuid_s[40];

    memset(reel, 0, sizeof(reel_t));

    strcpy(reel->annotation, dcp.annotation);

    /* Generate UUIDs */
    uuid_random(uuid_s);
    sprintf(reel->uuid, "%.36s", uuid_s);
}

int validate_reel(opendcp_t *opendcp, reel_t *reel, int reel_number) {
    int d = 0;
    int picture = 0;
    int duration_mismatch = 0;
    UNUSED(opendcp);

    /* change reel to 1 based for user */
    reel_number += 1;

    OPENDCP_LOG(LOG_DEBUG, "validate_reel: validating reel %d", reel_number);

    /* check if reel has a picture track */
    if (reel->main_picture.essence_class == ACT_PICTURE) {
        picture++;
    }

    if (picture < 1) {
        OPENDCP_LOG(LOG_ERROR, "Reel %d has no picture track", reel_number);
        return OPENDCP_NO_PICTURE_TRACK;
    }
    else if (picture > 1) {
        OPENDCP_LOG(LOG_ERROR, "Reel %d has multiple picture tracks", reel_number);
        return OPENDCP_MULTIPLE_PICTURE_TRACK;
    }

    /* check specification */
    if (reel->main_sound.duration && reel->main_picture.xml_ns != reel->main_sound.xml_ns) {
        OPENDCP_LOG(LOG_ERROR, "Warning DCP specification mismatch in assets. Please make sure all assets are MXF Interop or SMPTE");
        return OPENDCP_SPECIFICATION_MISMATCH;
    }

    if (reel->main_subtitle.duration && reel->main_picture.xml_ns != reel->main_subtitle.xml_ns) {
        OPENDCP_LOG(LOG_ERROR, "Warning DCP specification mismatch in assets. Please make sure all assets are MXF Interop or SMPTE");
        return OPENDCP_SPECIFICATION_MISMATCH;
    }

    /* check durations */
    d = reel->main_picture.duration;

    if (reel->main_sound.duration && reel->main_sound.duration != d) {
        duration_mismatch = 1;

        if (reel->main_sound.duration < d) {
            d = reel->main_sound.duration;
        }
    }

    if (reel->main_subtitle.duration && reel->main_subtitle.duration != d) {
        duration_mismatch = 1;

        if (reel->main_subtitle.duration < d) {
            d = reel->main_subtitle.duration;
        }
    }

    if (duration_mismatch) {
        reel->main_picture.duration = d;
        reel->main_sound.duration = d;
        reel->main_subtitle.duration = d;
        OPENDCP_LOG(LOG_WARN, "Asset duration mismatch, adjusting all durations to shortest asset duration of %d frames", d);
    }

    return OPENDCP_NO_ERROR;
}

void add_reel_to_cpl(cpl_t *cpl, reel_t reel) {
    memcpy(&cpl->reel[cpl->reel_count], &reel, sizeof(reel_t));
    cpl->reel_count++;
}

int add_asset(opendcp_t *opendcp, asset_t *asset, char *filename) {
    struct stat st;
    FILE   *fp;
    int    result;

    OPENDCP_LOG(LOG_INFO, "Adding asset %s", filename);

    init_asset(asset);

    /* check if file exists */
    if ((fp = fopen(filename, "r")) == NULL) {
        OPENDCP_LOG(LOG_ERROR, "add_asset: Could not open file: %s", filename);
        return OPENDCP_FILEOPEN;
    }
    else {
        fclose (fp);
    }

    sprintf(asset->filename, "%s", filename);
    sprintf(asset->annotation, "%s", basename(filename));

    /* get file size */
    stat(filename, &st);
    sprintf(asset->size, "%"PRIu64, st.st_size);

    /* read asset information */
    OPENDCP_LOG(LOG_DEBUG, "add_asset: Reading %s asset information", filename);

    result = read_asset_info(asset);

    if (result == OPENDCP_ERROR) {
        OPENDCP_LOG(LOG_ERROR, "%s is not a proper essence file", filename);
        return OPENDCP_INVALID_TRACK_TYPE;
    }

    /* force aspect ratio, if specified */
    if (strcmp(opendcp->dcp.aspect_ratio, "") ) {
        sprintf(asset->aspect_ratio, "%s", opendcp->dcp.aspect_ratio);
    }

    /* Set duration, if specified */
    if (opendcp->duration) {
        if  (opendcp->duration < asset->duration) {
            asset->duration = opendcp->duration;
        }
        else {
            OPENDCP_LOG(LOG_WARN, "Desired duration %d cannot be greater than assset duration %d, ignoring value", opendcp->duration, asset->duration);
        }
    }

    /* Set entry point, if specified */
    if (opendcp->entry_point) {
        if (opendcp->entry_point < asset->duration) {
            asset->entry_point = opendcp->entry_point;
        }
        else {
            OPENDCP_LOG(LOG_WARN, "Desired entry point %d cannot be greater than assset duration %d, ignoring value", opendcp->entry_point, asset->duration);
        }
    }

    /* calculate digest */
    // calculate_digest(opendcp, filename, asset->digest);

    return OPENDCP_NO_ERROR;
}

int add_asset_to_reel(opendcp_t *opendcp, reel_t *reel, asset_t asset) {
    int result;

    OPENDCP_LOG(LOG_INFO, "Adding asset to reel");

    if (opendcp->ns == XML_NS_UNKNOWN) {
        opendcp->ns = asset.xml_ns;
        OPENDCP_LOG(LOG_DEBUG, "add_asset_to_reel: Label type detected: %d", opendcp->ns);
    }
    else {
        if (opendcp->ns != asset.xml_ns) {
            OPENDCP_LOG(LOG_ERROR, "Warning DCP specification mismatch in assets. Please make sure all assets are MXF Interop or SMPTE");
            return OPENDCP_SPECIFICATION_MISMATCH;
        }
    }

    result = get_asset_type(asset);

    switch (result) {
        case ACT_PICTURE:
            OPENDCP_LOG(LOG_DEBUG, "add_asset_to_reel: adding picture");
            reel->main_picture = asset;
            break;

        case ACT_SOUND:
            OPENDCP_LOG(LOG_DEBUG, "add_asset_to_reel: adding sound");
            reel->main_sound = asset;
            break;

        case ACT_TIMED_TEXT:
            OPENDCP_LOG(LOG_DEBUG, "add_asset_to_reel: adding subtitle");
            reel->main_subtitle = asset;
            break;

        default:
            return OPENDCP_ERROR;
    }

    return OPENDCP_NO_ERROR;
}
