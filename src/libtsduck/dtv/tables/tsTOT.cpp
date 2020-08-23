//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2020, Thierry Lelegard
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------

#include "tsTOT.h"
#include "tsBinaryTable.h"
#include "tsTablesDisplay.h"
#include "tsPSIRepository.h"
#include "tsPSIBuffer.h"
#include "tsDuckContext.h"
#include "tsxmlElement.h"
TSDUCK_SOURCE;

#define MY_XML_NAME u"TOT"
#define MY_CLASS ts::TOT
#define MY_TID ts::TID_TOT
#define MY_PID ts::PID_TOT
#define MY_STD ts::Standards::DVB

TS_REGISTER_TABLE(MY_CLASS, {MY_TID}, MY_STD, MY_XML_NAME, MY_CLASS::DisplaySection, nullptr, {MY_PID});


//----------------------------------------------------------------------------
// Constructors
//----------------------------------------------------------------------------

ts::TOT::TOT(const Time& utc_time_) :
    AbstractTable(MY_TID, MY_XML_NAME, MY_STD),
    utc_time(utc_time_),
    regions(),
    descs(this)
{
}

ts::TOT::TOT(DuckContext& duck, const BinaryTable& table) :
    TOT()
{
    deserialize(duck, table);
}

ts::TOT::TOT(const TOT& other) :
    AbstractTable(other),
    utc_time(other.utc_time),
    regions(other.regions),
    descs(this, other.descs)
{
}


//----------------------------------------------------------------------------
// Check if the sections of this table have a trailing CRC32.
//----------------------------------------------------------------------------

bool ts::TOT::useTrailingCRC32() const
{
    // A TOT is a short section with a CRC32.
    return true;
}


//----------------------------------------------------------------------------
// Clear the content of the table.
//----------------------------------------------------------------------------

void ts::TOT::clearContent()
{
    utc_time.clear();
    regions.clear();
    descs.clear();
}


//----------------------------------------------------------------------------
// Return the local time according to a region description
//----------------------------------------------------------------------------

ts::Time ts::TOT::localTime(const Region& reg) const
{
    // Add local time offset in milliseconds
    return utc_time + MilliSecond(reg.time_offset) * 60 * MilliSecPerSec;
}


//----------------------------------------------------------------------------
// Format a time offset in minutes
//----------------------------------------------------------------------------

ts::UString ts::TOT::timeOffsetFormat(int minutes)
{
    return UString::Format(u"%s%02d:%02d", {minutes < 0 ? u"-" : u"", ::abs(minutes) / 60, ::abs(minutes) % 60});
}


//----------------------------------------------------------------------------
// Add descriptors, filling regions from local_time_offset_descriptor's.
//----------------------------------------------------------------------------

void ts::TOT::addDescriptors(DuckContext& duck, const DescriptorList& dlist)
{
    // Loop on all descriptors.
    for (size_t index = 0; index < dlist.count(); ++index) {
        if (!dlist[index].isNull() && dlist[index]->isValid()) {
            if (dlist[index]->tag() != DID_LOCAL_TIME_OFFSET) {
                // Not a local_time_offset_descriptor, add to descriptor list.
                descs.add(dlist[index]);
            }
            else {
                // Decode local_time_offset_descriptor in the list of regions.
                LocalTimeOffsetDescriptor lto(duck, *dlist[index]);
                if (lto.isValid()) {
                    regions.insert(regions.end(), lto.regions.begin(), lto.regions.end());
                }
            }
        }
    }
}


//----------------------------------------------------------------------------
// Deserialization
//----------------------------------------------------------------------------

void ts::TOT::deserializePayload(PSIBuffer& buf, const Section& section)
{
    // A TOT section is a short section with a CRC32. But it has already been checked
    // and removed from the buffer since TOT::useTrailingCRC32() returns true.

    // Get UTC time.
    utc_time = buf.getFullMJD();

    // In Japan, the time field is in fact a JST time, convert it to UTC.
    if ((buf.duck().standards() & Standards::JAPAN) == Standards::JAPAN) {
        utc_time = utc_time.JSTToUTC();
    }

    // Get descriptor list.
    DescriptorList dlist(nullptr);
    buf.getDescriptorListWithLength(dlist);

    // Split between actual descriptors and regions.
    addDescriptors(buf.duck(), dlist);
}


//----------------------------------------------------------------------------
// Serialization
//----------------------------------------------------------------------------

void ts::TOT::serializePayload(BinaryTable& table, PSIBuffer& buf) const
{
    // Encode the data in MJD in the payload.
    // In Japan, the time field is in fact a JST time, convert UTC to JST before serialization.
    if ((buf.duck().standards() & Standards::JAPAN) == Standards::JAPAN) {
        buf.putFullMJD(utc_time.UTCToJST());
    }
    else {
        buf.putFullMJD(utc_time);
    }

    // Build a descriptor list.
    DescriptorList dlist(nullptr);

    // Add all regions in one or more local_time_offset_descriptor.
    LocalTimeOffsetDescriptor lto;
    for (RegionVector::const_iterator it = regions.begin(); it != regions.end(); ++it) {
        lto.regions.push_back(*it);
        if (lto.regions.size() >= LocalTimeOffsetDescriptor::MAX_REGION) {
            dlist.add(buf.duck(), lto);
            lto.regions.clear();
        }
    }
    if (!lto.regions.empty()) {
        dlist.add(buf.duck(), lto);
    }

    // Append the "other" descriptors to the list
    dlist.add(descs);

    // Insert descriptor list (with leading length field).
    buf.putPartialDescriptorListWithLength(dlist);

    // A TOT section is a short section with a CRC32. But it will be
    // automatically added since TOT::useTrailingCRC32() returns true.
}


//----------------------------------------------------------------------------
// A static method to display a TOT section.
//----------------------------------------------------------------------------

void ts::TOT::DisplaySection(TablesDisplay& display, const ts::Section& section, int indent)
{
    DuckContext& duck(display.duck());
    std::ostream& strm(duck.out());
    const std::string margin(indent, ' ');
    PSIBuffer buf(duck, section.payload(), section.payloadSize());

    if (buf.remainingReadBytes() >= 5) {
        strm << margin << "UTC time: " << buf.getFullMJD().format(Time::DATETIME) << std::endl;
        display.displayDescriptorListWithLength(section, buf, indent);
        display.displayCRC32(section, buf, indent);
    }

    display.displayExtraData(buf, indent);
}


//----------------------------------------------------------------------------
// XML serialization
//----------------------------------------------------------------------------

void ts::TOT::buildXML(DuckContext& duck, xml::Element* root) const
{
    root->setDateTimeAttribute(u"UTC_time", utc_time);

    // Add one local_time_offset_descriptor per set of regions.
    // Each local_time_offset_descriptor can contain up to 19 regions.
    LocalTimeOffsetDescriptor lto;
    for (RegionVector::const_iterator it = regions.begin(); it != regions.end(); ++it) {
        lto.regions.push_back(*it);
        if (lto.regions.size() >= LocalTimeOffsetDescriptor::MAX_REGION) {
            // The descriptor is full, flush it in the list.
            lto.toXML(duck, root);
            lto.regions.clear();
        }
    }
    if (!lto.regions.empty()) {
        // The descriptor is not empty, flush it in the list.
        lto.toXML(duck, root);
    }

    // Add other descriptors.
    descs.toXML(duck, root);
}


//----------------------------------------------------------------------------
// XML deserialization
//----------------------------------------------------------------------------

bool ts::TOT::analyzeXML(DuckContext& duck, const xml::Element* element)
{
    DescriptorList orig(this);

    // Get all descriptors in a separated list.
    const bool ok = element->getDateTimeAttribute(utc_time, u"UTC_time", true) && orig.fromXML(duck, element);

    // Then, split local_time_offset_descriptor and others.
    addDescriptors(duck, orig);
    return ok;
}
