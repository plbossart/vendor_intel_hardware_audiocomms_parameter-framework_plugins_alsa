/*
 * INTEL CONFIDENTIAL
 * Copyright © 2011 Intel
 * Corporation All Rights Reserved.
 *
 * The source code contained or described herein and all documents related to
 * the source code ("Material") are owned by Intel Corporation or its suppliers
 * or licensors. Title to the Material remains with Intel Corporation or its
 * suppliers and licensors. The Material contains trade secrets and proprietary
 * and confidential information of Intel or its suppliers and licensors. The
 * Material is protected by worldwide copyright and trade secret laws and
 * treaty provisions. No part of the Material may be used, copied, reproduced,
 * modified, published, uploaded, posted, transmitted, distributed, or
 * disclosed in any way without Intel’s prior express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery
 * of the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be
 * express and approved by Intel in writing.
 *
 */
#include "LegacyAmixerControl.hpp"
#include "InstanceConfigurableElement.h"
#include "ParameterType.h"
#include "BitParameterBlockType.h"
#include "MappingContext.h"
#include "AmixerMappingKeys.hpp"
#include "AutoLog.h"
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <alsa/asoundlib.h>
#include <sstream>

#ifdef ANDROID
extern "C"
{
int snd_ctl_hw_open(snd_ctl_t **handle, const char *name, int card, int mode);
}
#endif


#define base AmixerControl

LegacyAmixerControl::LegacyAmixerControl(
    const string &mappingValue,
    CInstanceConfigurableElement *instanceConfigurableElement,
    const CMappingContext &context)
    : base(mappingValue, instanceConfigurableElement, context)
{

}

bool LegacyAmixerControl::accessHW(bool receive, string &error)
{
    CAutoLog autoLog(getConfigurableElement(), "AMIXER", isDebugEnabled());

#ifdef SIMULATION
    if (receive) {

        memset(getBlackboardLocation(), 0, getSize());
    }
    log_info("%s AMIXER Element Instance: %s\t\t(Control Element: %s)",
             receive ? "Reading" : "Writing",
             getConfigurableElement()->getPath().c_str(),
             getControlName().c_str());

    return true;
#endif

    int ret;
    // Mixer handle
    snd_ctl_t *sndCtrl;
    uint32_t value;
    uint32_t index;
    uint32_t elementCount;
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_value_t *control;

    logControlInfo(receive);

    // Check parameter type is ok (deferred error, no exceptions available :-()
    if (!isTypeSupported()) {

        error = "Parameter type not supported.";

        return false;
    }

    int cardNumber = getCardNumber();

    if (cardNumber < 0) {

        error = "Card " + getCardName() + " not found. Error: " + strerror(cardNumber);

        return false;
    }
#ifdef ANDROID
    if ((ret = snd_ctl_hw_open(&sndCtrl, NULL, cardNumber, 0)) < 0) {

        error = snd_strerror(ret);

        return false;
    }
#else
    // Create device name
    ostringstream deviceName;

    deviceName << "hw:" << cardNumber;

    // Open sound control
    if ((ret = snd_ctl_open(&sndCtrl, deviceName.str().c_str(), 0)) < 0) {

        error = snd_strerror(ret);

        return false;
    }
#endif

    // Allocate in stack
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_value_alloca(&control);

    // Set interface
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);

    string controlName = getControlName();

    // Set name or id
    if (isdigit(controlName[0])) {

        snd_ctl_elem_id_set_numid(id, asInteger(controlName));
    } else {

        snd_ctl_elem_id_set_name(id, controlName.c_str());
    }
    // Init info id
    snd_ctl_elem_info_set_id(info, id);

    if (hasIndex()) {

        snd_ctl_elem_id_set_index(id, getIndex());
    }

    // Get info
    if ((ret = snd_ctl_elem_info(sndCtrl, info)) < 0) {

        error = "AMIXER: Unable to get element info " + controlName +
                ": " + snd_strerror(ret);

        // Close sound control
        snd_ctl_close(sndCtrl);

        return false;
    }
    // Get type
    snd_ctl_elem_type_t eType = snd_ctl_elem_info_get_type(info);

    // Get element count
    elementCount = snd_ctl_elem_info_get_count(info);

    uint32_t scalarSize = getScalarSize();

    // If size defined in the PFW different from alsa mixer control size, return an error
    if (elementCount * scalarSize != getSize()) {

        error = "AMIXER: Control element count (" + asString(elementCount) +
                ") and configurable scalar element count (" +
                asString(getSize() / scalarSize) + ") mismatch";

        // Close sound control
        snd_ctl_close(sndCtrl);

        return false;
    }
    // Set value id
    snd_ctl_elem_value_set_id(control, id);

    if (receive) {

        // Read element
        if ((ret = snd_ctl_elem_read(sndCtrl, control)) < 0) {

            error = "AMIXER: Unable to read element " + controlName +
                    ": " + snd_strerror(ret);

            // Close sound control
            snd_ctl_close(sndCtrl);

            return false;
        }
        // Go through all indexes
        for (index = 0; index < elementCount; index++) {

            switch (eType) {
            case SND_CTL_ELEM_TYPE_BOOLEAN:
                value = snd_ctl_elem_value_get_boolean(control, index);
                break;
            case SND_CTL_ELEM_TYPE_INTEGER:
                value = snd_ctl_elem_value_get_integer(control, index);
                break;
            case SND_CTL_ELEM_TYPE_INTEGER64:
                value = snd_ctl_elem_value_get_integer64(control, index);
                break;
            case SND_CTL_ELEM_TYPE_ENUMERATED:
                value = snd_ctl_elem_value_get_enumerated(control, index);
                break;
            case SND_CTL_ELEM_TYPE_BYTES:
                value = snd_ctl_elem_value_get_byte(control, index);
                break;
            default:
                error = "AMIXER: Unknown control element type while reading alsa element " +
                        controlName;
                return false;
            }

            if (isDebugEnabled()) {

                log_info("Reading alsa element %s,%d, index %u with value %u",
                         controlName.c_str(), getIndex(), index, value);
            }

            // Write data to blackboard (beware this code is OK on Little Endian machines only)
            toBlackboard(value);
        }

    } else {

        // Go through all indexes
        for (index = 0; index < elementCount; index++) {

            // Read data from blackboard (beware this code is OK on Little Endian machines only)
            value = fromBlackboard();

            if (isDebugEnabled()) {

                log_info("Writing alsa element %s,%d, index %u with value %u",
                         controlName.c_str(), getIndex(), index, value);
            }

            switch (eType) {
            case SND_CTL_ELEM_TYPE_BOOLEAN:
                snd_ctl_elem_value_set_boolean(control, index, value);
                break;
            case SND_CTL_ELEM_TYPE_INTEGER:
                snd_ctl_elem_value_set_integer(control, index, value);
                break;
            case SND_CTL_ELEM_TYPE_INTEGER64:
                snd_ctl_elem_value_set_integer64(control, index, value);
                break;
            case SND_CTL_ELEM_TYPE_ENUMERATED:
                snd_ctl_elem_value_set_enumerated(control, index, value);
                break;
            case SND_CTL_ELEM_TYPE_BYTES:
                snd_ctl_elem_value_set_byte(control, index, value);
                break;
            default:
                error = "AMIXER: Unknown control element type while writing alsa element " +
                        controlName;
                return false;
            }
        }

        // Write element
        if ((ret = snd_ctl_elem_write(sndCtrl, control)) < 0) {

            error = "AMIXER: Unable to write element " + controlName +
                    ": " + snd_strerror(ret);


            // Close sound control
            snd_ctl_close(sndCtrl);

            return false;
        }
    }
    // Close sound control
    snd_ctl_close(sndCtrl);

    return true;
}