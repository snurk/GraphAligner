//split this here so modifying GraphAligner.h doesn't require recompiling every cpp file

#ifndef GraphAlignerWrapper_h
#define GraphAlignerWrapper_h

#include "AlignmentGraph.h"
#include "vg.pb.h"

class AlignmentResult
{
public:
	AlignmentResult()
	{
	}
	AlignmentResult(vg::Alignment alignment, bool alignmentFailed, size_t cellsProcessed, size_t ms) :
	alignment(alignment),
	alignmentFailed(alignmentFailed),
	cellsProcessed(cellsProcessed),
	elapsedMilliseconds(ms)
	{
	}
	vg::Alignment alignment;
	bool alignmentFailed;
	size_t cellsProcessed;
	size_t elapsedMilliseconds;
	size_t firstPartStart;
	size_t firstPartEnd;
	size_t secondPartStart;
	size_t secondPartEnd;
	size_t secondPartFirstIndex;
};

AlignmentResult AlignOneWay(const AlignmentGraph& graph, const std::string& seq_id, const std::string& sequence, int dynamicWidth, size_t dynamicRowStart);
AlignmentResult AlignOneWay(const AlignmentGraph& graph, const std::string& seq_id, const std::string& sequence, int dynamicWidth, size_t dynamicRowStart, const std::vector<std::pair<int, size_t>>& seedHits, int startBandwidth);

#endif