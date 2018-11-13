#ifndef MummerSeeder_h
#define MummerSeeder_h

#include <vector>
#include <string>
#include <mummer/sparseSA.hpp>
#include <mummer/fasta.hpp>
#include "GfaGraph.h"
#include "GraphAlignerWrapper.h"
#include "vg.pb.h"

class MummerSeeder
{
public:
	MummerSeeder(const GfaGraph& graph, size_t minLen, const std::string& cachePrefix);
	MummerSeeder(const vg::Graph& graph, size_t minLen, const std::string& cachePrefix);
	std::vector<SeedHit> getMemSeeds(std::string sequence, size_t maxCount) const;
	std::vector<SeedHit> getMumSeeds(std::string sequence, size_t maxCount) const;
private:
	std::vector<SeedHit> matchesToSeeds(size_t seqLen, const std::vector<mummer::mummer::match_t>& fwmatches, const std::vector<mummer::mummer::match_t>& bwmatches) const;
	void revcompInPlace(std::string& seq) const;
	size_t getNodeIndex(size_t indexPos) const;
	size_t nodeLength(size_t indexPos) const;
	void initTree(const GfaGraph& graph, size_t minLen);
	void initTree(const vg::Graph& graph, size_t minLen);
	void saveTo(const std::string& cachePrefix) const;
	void loadFrom(const std::string& cachePrefix);
	size_t minLen;
	std::string seq;
	std::unique_ptr<mummer::mummer::sparseSA> matcher;
	std::vector<size_t> nodePositions;
	std::vector<int> nodeIDs;
};

#endif
