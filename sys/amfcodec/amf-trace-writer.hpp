#ifndef AMFTRACEWRITER_H
#define AMFTRACEWRITER_H

#include <string>
#include <memory>
#include <sstream>
#include <ios>
#include "AMF/include/core/Result.h"
#include "AMF/include/core/Trace.h"
#include "AMF/include/core/Data.h"
#define OBS_AMF_TRACE_WRITER L"OBS_AMF_TRACE_WRITER"

class GstAMFTraceWriter : public amf::AMFTraceWriter {
public:
	GstAMFTraceWriter() {}
	virtual void AMF_CDECL_CALL Write(const wchar_t *scope,
					  const wchar_t *message) override;
	virtual void AMF_CDECL_CALL Flush() override {}
};
#endif // AMFTRACEWRITER_H
