//http://biorxiv.org/content/early/2017/04/06/124941
#include <algorithm>
#include <string>
#include <vector>
#include <cassert>
#include <cmath>
#include "vg.pb.h"

template <typename LengthType, typename ScoreType>
class GraphAligner
{
public:
	typedef std::pair<LengthType, LengthType> MatrixPosition;
	class MatrixSlice
	{
	public:
		std::vector<ScoreType> M;
		std::vector<ScoreType> Q;
		std::vector<ScoreType> R;
		std::vector<MatrixPosition> Rbacktrace;
		std::vector<MatrixPosition> Qbacktrace;
		std::vector<std::vector<MatrixPosition>> backtrace;
	};

	GraphAligner() :
	nodeStart(),
	indexToNode(),
	nodeLookup(),
	nodeIDs(),
	inNeighbors(),
	nodeSequences(),
	gapStartPenalty(1),
	gapContinuePenalty(1)
	{
		//add the dummy node as the first node
		nodeIDs.push_back(0);
		nodeStart.push_back(nodeSequences.size());
		inNeighbors.emplace_back();
		nodeSequences.push_back('N');
		indexToNode.resize(nodeSequences.size(), nodeStart.size()-1);
		nodeEnd.emplace_back(nodeSequences.size());
		notInOrder.push_back(false);
	}
	
	void AddNode(int nodeId, std::string sequence)
	{
		assert(std::numeric_limits<LengthType>::max() - sequence.size() > nodeSequences.size());
		nodeLookup[nodeId] = nodeStart.size();
		nodeIDs.push_back(nodeId);
		nodeStart.push_back(nodeSequences.size());
		inNeighbors.emplace_back();
		nodeSequences.insert(nodeSequences.end(), sequence.begin(), sequence.end());
		indexToNode.resize(nodeSequences.size(), nodeStart.size()-1);
		nodeEnd.emplace_back(nodeSequences.size());
		notInOrder.push_back(false);
		assert(nodeIDs.size() == nodeStart.size());
		assert(nodeStart.size() == inNeighbors.size());
		assert(inNeighbors.size() == nodeEnd.size());
		assert(nodeEnd.size() == notInOrder.size());
		assert(nodeSequences.size() == indexToNode.size());
	}
	
	void AddEdge(int node_id_from, int node_id_to)
	{
		assert(nodeLookup.count(node_id_from) > 0);
		assert(nodeLookup.count(node_id_to) > 0);
		auto from = nodeLookup[node_id_from];
		auto to = nodeLookup[node_id_to];
		assert(to >= 0);
		assert(from >= 0);
		assert(to < inNeighbors.size());
		assert(from < nodeStart.size());
		inNeighbors[to].push_back(from);
		if (from >= to)
		{
			notInOrder[to] = true;
		}
	}

	void Finalize()
	{
		//add an edge from the dummy node to all nodes without an in-edge
		for (LengthType i = 0; i < nodeSequences.size(); i++)
		{
			if (nodeStart[indexToNode[i]] == i)
			{
				if (inNeighbors[indexToNode[i]].size() == 0)
				{
					inNeighbors[indexToNode[i]].push_back(0);
				}
			}
		}
	}

	vg::Alignment AlignOneWay(const std::string& seq_id, const std::string& sequence, bool reverse) const
	{
		auto trace = backtrackWithSquareRootSlices(seq_id, sequence);
		auto result = traceToAlignment(seq_id, trace, reverse);
		return result;
	}

private:

	vg::Alignment traceToAlignment(const std::string& seq_id, const std::pair<ScoreType, std::vector<MatrixPosition>>& traceWithScore, bool reverse) const
	{
		auto& trace = traceWithScore.second;
		vg::Alignment result;
		result.set_name(seq_id);
		auto path = new vg::Path;
		result.set_allocated_path(path);
		size_t oldNode = indexToNode[trace[0].first];
		int rank = 0;
		auto vgmapping = path->add_mapping();
		auto position = new vg::Position;
		vgmapping->set_allocated_position(position);
		vgmapping->set_rank(rank);
		position->set_node_id(nodeIDs[oldNode]);
		if (reverse) position->set_is_reverse(true);
		for (size_t i = 1; i < trace.size(); i++)
		{
			if (indexToNode[trace[i].first] == oldNode) continue;
			oldNode = indexToNode[trace[i].first];
			rank++;
			vgmapping = path->add_mapping();
			position = new vg::Position;
			vgmapping->set_allocated_position(position);
			vgmapping->set_rank(rank);
			position->set_node_id(nodeIDs[oldNode]);
			if (reverse) position->set_is_reverse(true);
		}
		result.set_score(traceWithScore.first);
		return result;
	}

	std::pair<ScoreType, std::vector<MatrixPosition>> backtrace(const std::vector<ScoreType>& Mslice, const std::vector<std::vector<MatrixPosition>>& backtrace) const
	{
		assert(backtrace.size() == nodeSequences.size());
		assert(backtrace[0].size() > 0);
		std::vector<MatrixPosition> trace;
		MatrixPosition currentPosition = std::make_pair(0, backtrace[0].size()-1);
		//start at the highest value at end of read
		for (size_t i = 0; i < Mslice.size(); i++)
		{
			MatrixPosition candidatePosition = std::make_pair(i, backtrace[0].size()-1);
			if (Mslice[candidatePosition.first] > Mslice[currentPosition.first])
			{
				currentPosition = candidatePosition;
			}
		}
		auto score = Mslice[currentPosition.first];
		trace.push_back(currentPosition);
		while (currentPosition.second > 0)
		{
			assert(currentPosition.second >= 0);
			assert(currentPosition.second < backtrace[0].size());
			assert(currentPosition.first >= 0);
			assert(currentPosition.first < nodeSequences.size());
			auto newPos = backtrace[currentPosition.first][currentPosition.second];
			//If we're at the dummy node, we have to stay there
			assert(currentPosition.first == 0 ? (newPos.first == 0) : true);
			assert(newPos.second < currentPosition.second || (newPos.second == currentPosition.second && newPos.first < currentPosition.first));
			currentPosition = newPos;
			trace.push_back(currentPosition);
		}
		std::reverse(trace.begin(), trace.end());
		return std::make_pair(score, trace);
	}

	MatrixSlice getScoreAndBacktraceMatrixSlice(const std::string& seq_id, const std::string& sequence, bool hasWrongOrders, const std::vector<LengthType>& nodeOrdering, const std::vector<std::vector<LengthType>>& distanceMatrix, const MatrixSlice& previous, LengthType start, LengthType end) const
	{
		std::vector<ScoreType> M1;
		std::vector<ScoreType> M2;
		std::vector<ScoreType> Q1;
		std::vector<ScoreType> Q2;
		std::vector<ScoreType> R1;
		std::vector<ScoreType> R2;
		std::vector<MatrixPosition> Rbacktrace1;
		std::vector<MatrixPosition> Rbacktrace2;
		assert(previous.M.size() == nodeSequences.size());
		assert(previous.R.size() == nodeSequences.size());
		assert(previous.Q.size() == nodeSequences.size());
		assert(previous.Rbacktrace.size() == nodeSequences.size());
		assert(previous.Qbacktrace.size() == nodeSequences.size());
		assert(previous.backtrace.size() == nodeSequences.size());
		MatrixSlice result;
		std::vector<std::vector<MatrixPosition>> backtrace;
		std::vector<MatrixPosition> Qbacktrace;
		M1.resize(nodeSequences.size());
		Q1.resize(nodeSequences.size());
		R1.resize(nodeSequences.size());
		Rbacktrace1.resize(nodeSequences.size());
		M2.resize(nodeSequences.size());
		Q2.resize(nodeSequences.size());
		R2.resize(nodeSequences.size());
		Rbacktrace2.resize(nodeSequences.size());
		Qbacktrace.resize(nodeSequences.size());
		backtrace.resize(nodeSequences.size());
		std::vector<ScoreType>& currentM = M1;
		std::vector<ScoreType>& previousM = M2;
		std::vector<ScoreType>& currentQ = Q1;
		std::vector<ScoreType>& previousQ = Q2;
		std::vector<ScoreType>& currentR = R1;
		std::vector<ScoreType>& previousR = R2;
		std::vector<MatrixPosition>& currentRbacktrace = Rbacktrace1;
		std::vector<MatrixPosition>& previousRbacktrace = Rbacktrace2;
		for (LengthType w = 0; w < nodeSequences.size(); w++)
		{
			previousM[w] = previous.M[w];
			previousQ[w] = previous.Q[w];
			previousR[w] = previous.R[w];
			Qbacktrace[w] = previous.Qbacktrace[w];
			previousRbacktrace[w] = previous.Rbacktrace[w];
			backtrace[w].resize(end-start);
			backtrace[w][0] = previous.backtrace[w].back();
		}
		currentR[0] = std::numeric_limits<ScoreType>::min() + gapContinuePenalty + 100;
		previousR[0] = std::numeric_limits<ScoreType>::min() + gapContinuePenalty + 100;
		currentM[0] = -gapPenalty(start + 1);
		previousM[0] = -gapPenalty(start);
		for (LengthType j = 1; j < end - start; j++)
		{
			std::vector<std::pair<LengthType, ScoreType>> Rhelper;
			if (hasWrongOrders) Rhelper = getRHelper(j, previousM, sequence);

			for (LengthType w : nodeOrdering)
			{
				auto nodeIndex = indexToNode[w];
				currentQ[w] = previousQ[w] - gapContinuePenalty;
				if (currentM[w] - gapPenalty(1) > currentQ[w])
				{
					currentQ[w] = previousM[w] - gapPenalty(1);
					Qbacktrace[w] = std::make_pair(w, j-1 + start);
				}
				if (w == nodeStart[nodeIndex] && notInOrder[nodeIndex])
				{
					assert(hasWrongOrders);
					auto rr = fullR(w, j, Rhelper, distanceMatrix, start);
					currentR[w] = rr.first;
					currentRbacktrace[w] = rr.second;
					assert(currentRbacktrace[w].second < (j + start) || (currentRbacktrace[w].second == (j + start) && backtrace[w][j].first < w));
				}
				else
				{
					auto rr = recurrenceR(w, j, currentM, currentR, currentRbacktrace, start);
					currentR[w] = rr.first;
					currentRbacktrace[w] = rr.second;
					assert(currentRbacktrace[w].second < (j + start) || (currentRbacktrace[w].second == (j + start) && backtrace[w][j].first < w));
				}
				backtrace[w][j] = Qbacktrace[w];
				assert(backtrace[w][j].second < (j + start) || (backtrace[w][j].second == (j + start) && backtrace[w][j].first < w));
				currentM[w] = currentQ[w];
				if (currentR[w] > currentM[w])
				{
					currentM[w] = currentR[w];
					backtrace[w][j] = currentRbacktrace[w];
					assert(backtrace[w][j].second < (j + start) || (backtrace[w][j].second == (j + start) && backtrace[w][j].first < w));
				}
				if (w == nodeStart[nodeIndex])
				{
					for (size_t i = 0; i < inNeighbors[nodeIndex].size(); i++)
					{
						auto u = nodeEnd[inNeighbors[nodeIndex][i]]-1;
						if (previousM[u]+matchScore(nodeSequences[w], sequence[j + start]) > currentM[w])
						{
							currentM[w] = previousM[u]+matchScore(nodeSequences[w], sequence[j + start]);
							backtrace[w][j] = std::make_pair(u, j-1 + start);
							assert(backtrace[w][j].second < (j + start) || (backtrace[w][j].second == (j + start) && backtrace[w][j].first < w));
						}
					}
				}
				else
				{
					LengthType u = w-1;
					if (previousM[u]+matchScore(nodeSequences[w], sequence[j + start]) > currentM[w])
					{
						currentM[w] = previousM[u]+matchScore(nodeSequences[w], sequence[j + start]);
						backtrace[w][j] = std::make_pair(u, j-1 + start);
						assert(backtrace[w][j].second < (j + start) || (backtrace[w][j].second == (j + start) && backtrace[w][j].first < w));
					}
				}
				assert(currentM[w] >= -std::numeric_limits<ScoreType>::min() + 100);
				assert(currentM[w] <= std::numeric_limits<ScoreType>::max() - 100);
				assert(backtrace[w][j].second < (j + start) || (backtrace[w][j].second == (j + start) && backtrace[w][j].first < w));
			}
			std::swap(currentM, previousM);
			std::swap(currentQ, previousQ);
			std::swap(currentR, previousR);
			std::swap(currentRbacktrace, previousRbacktrace);
		}
		result.M.reserve(nodeSequences.size());
		result.Q.reserve(nodeSequences.size());
		result.R.reserve(nodeSequences.size());
		result.Rbacktrace.reserve(nodeSequences.size());
		result.backtrace = backtrace;
		result.Qbacktrace = Qbacktrace;
		for (LengthType w = 0; w < nodeSequences.size(); w++)
		{
			LengthType j = end-start-1;
			result.M.push_back(currentM[w]);
			result.Q.push_back(currentQ[w]);
			result.R.push_back(currentR[w]);
			result.Rbacktrace.push_back(currentRbacktrace[w]);
		}
		return result;
	}

	void addBacktraceMatrix(std::vector<std::vector<MatrixPosition>>& backtrace, const std::vector<std::vector<MatrixPosition>>& addThese) const
	{
		assert(backtrace.size() == nodeSequences.size());
		assert(backtrace.size() == addThese.size());
		assert(backtrace.size() > 0);
		assert(addThese[0].size() > 1);
		for (LengthType w = 0; w < backtrace.size(); w++)
		{
			backtrace[w].reserve(backtrace[w].size() + addThese[w].size());
			assert(w == 0 || addThese[w].size() == addThese[w-1].size());
			for (LengthType j = 1; j < addThese[w].size(); j++)
			{
				backtrace[w].push_back(addThese[w][j]);
			}
		}
	}

	std::pair<ScoreType, std::vector<MatrixPosition>> backtrackWithSquareRootSlices(const std::string& seq_id, const std::string& sequence) const
	{
		std::vector<std::vector<LengthType>> distanceMatrix = getDistanceMatrix();
		bool hasWrongOrders = false;
		std::vector<LengthType> nodeOrdering;
		nodeOrdering.reserve(nodeSequences.size());
		for (LengthType i = 1; i < nodeSequences.size(); i++)
		{
			auto nodeIndex = indexToNode[i];
			if (i == nodeStart[nodeIndex] && notInOrder[nodeIndex])
			{
				nodeOrdering.push_back(i);
				hasWrongOrders = true;
			}
		}
		for (LengthType i = 1; i < nodeSequences.size(); i++)
		{
			auto nodeIndex = indexToNode[i];
			if (!(i == nodeStart[nodeIndex] && notInOrder[nodeIndex]))
			{
				nodeOrdering.push_back(i);
			}
		}
		assert(nodeOrdering.size() == nodeSequences.size() - 1);
		MatrixSlice lastRow = getFirstSlice(seq_id, sequence, hasWrongOrders, nodeOrdering, distanceMatrix);
		int sliceSize = sqrt(sequence.size());
		std::vector<std::vector<MatrixPosition>> backtraceMatrix;
		backtraceMatrix.resize(nodeSequences.size());
		assert(lastRow.backtrace.size() == nodeSequences.size());
		for (LengthType w = 0; w < nodeSequences.size(); w++)
		{
			assert(lastRow.backtrace[w].size() == 1);
			backtraceMatrix[w].push_back(lastRow.backtrace[w][0]);
		}
		std::vector<ScoreType> lastRowScore;
		LengthType start = 1;
		while (start < sequence.size())
		{
			LengthType end = start + sliceSize;
			if (end > sequence.size()) end = sequence.size();
			auto slice = getScoreAndBacktraceMatrixSlice(seq_id, sequence, hasWrongOrders, nodeOrdering, distanceMatrix, lastRow, start-1, end);
			addBacktraceMatrix(backtraceMatrix, slice.backtrace);
			lastRowScore = slice.M;
			lastRow = std::move(slice);
			start = end;
		}
		auto result = backtrace(lastRow.M, backtraceMatrix);
		return result;
	}

	MatrixSlice getFirstSlice(const std::string& seq_id, const std::string& sequence, bool hasWrongOrders, const std::vector<LengthType>& nodeOrdering, const std::vector<std::vector<LengthType>>& distanceMatrix) const
	{
		MatrixSlice result;
		result.M.resize(nodeSequences.size(), 0);
		result.R.resize(nodeSequences.size(), 0);
		result.Q.resize(nodeSequences.size(), 0);
		result.Rbacktrace.reserve(nodeSequences.size());
		result.Qbacktrace.reserve(nodeSequences.size());
		result.backtrace.resize(nodeSequences.size());
		for (LengthType i = 0; i < nodeSequences.size(); i++)
		{
			result.backtrace[i].emplace_back(i, 0);
			result.Qbacktrace.emplace_back(i, 0);
			result.Rbacktrace.emplace_back(i, 0);
		}
		result.R[0] = std::numeric_limits<ScoreType>::min() + gapContinuePenalty + 100;
		assert(result.M.size() == nodeSequences.size());
		assert(result.R.size() == nodeSequences.size());
		assert(result.Q.size() == nodeSequences.size());
		assert(result.Rbacktrace.size() == nodeSequences.size());
		assert(result.Qbacktrace.size() == nodeSequences.size());
		assert(result.backtrace.size() == nodeSequences.size());
		return result;
	}

	std::vector<std::pair<LengthType, ScoreType>> getRHelper(LengthType j, const std::vector<ScoreType>& previousM, const std::string& sequence) const
	{
		std::vector<std::pair<LengthType, ScoreType>> result;
		for (LengthType v = 1; v < nodeSequences.size(); v++)
		{
			ScoreType maxValue = std::numeric_limits<ScoreType>::min();
			LengthType resultv = -1;
			auto otherNode = indexToNode[v];
			if (v == nodeStart[otherNode])
			{
				for (size_t neighbori = 0; neighbori < inNeighbors[otherNode].size(); neighbori++)
				{
					LengthType u = nodeEnd[inNeighbors[otherNode][neighbori]]-1;
					auto scoreHere = previousM[u] + matchScore(nodeSequences[v], sequence[j]);
					if (scoreHere > maxValue)
					{
						resultv = v;
						maxValue = scoreHere;
					}
				}
			}
			else
			{
				LengthType u = v-1;
				auto scoreHere = previousM[u]+matchScore(nodeSequences[v], sequence[j]);
				if (scoreHere > maxValue)
				{
					resultv = v;
					maxValue = scoreHere;
				}
			}
			assert(maxValue <= std::numeric_limits<ScoreType>::max() - 100);
			assert(maxValue >= -std::numeric_limits<ScoreType>::min() + 100);
			assert(resultv != -1);
			result.emplace_back(resultv, maxValue);
		}
		return result;
	}

	//compute R using the recurrence on page 3
	std::pair<ScoreType, MatrixPosition> recurrenceR(LengthType w, LengthType j, const std::vector<ScoreType>& currentM, const std::vector<ScoreType>& currentR, const std::vector<MatrixPosition>& currentRbacktrace, LengthType start) const
	{
		auto nodeIndex = indexToNode[w];
		assert(nodeStart[nodeIndex] != w || !notInOrder[nodeIndex]);
		MatrixPosition pos;
		ScoreType maxValue = std::numeric_limits<ScoreType>::min();
		if (nodeStart[nodeIndex] == w)
		{
			for (size_t i = 0; i < inNeighbors[nodeIndex].size(); i++)
			{
				auto u = nodeEnd[inNeighbors[nodeIndex][i]]-1;
				assert(u < w);
				if (currentM[u] - gapPenalty(1) > maxValue)
				{
					maxValue = currentM[u] - gapPenalty(1);
					pos = std::make_pair(u, j + start);
				}
				if (currentR[u] - gapContinuePenalty > maxValue)
				{
					maxValue = currentR[u] - gapContinuePenalty;
					pos = currentRbacktrace[u];
				}
			}
		}
		else
		{
			auto u = w-1;
			pos = currentRbacktrace[u];
			maxValue = currentR[u] - gapContinuePenalty;
			if (currentM[u] - gapPenalty(1) > maxValue)
			{
				pos = std::make_pair(w-1, j + start);
				maxValue = currentM[u] - gapPenalty(1);
			}
		}
		assert(maxValue >= -std::numeric_limits<ScoreType>::min() + 100);
		assert(maxValue <= std::numeric_limits<ScoreType>::max() - 100);
		return std::make_pair(maxValue, pos);
	}

	//compute R using the slow, full definition on page 3
	std::pair<ScoreType, MatrixPosition> fullR(LengthType w, LengthType j, const std::vector<std::pair<LengthType, ScoreType>>& RHelper, const std::vector<std::vector<LengthType>>& distanceMatrix, LengthType start) const
	{
		assert(j > 0);
		assert(w > 0);
		auto nodeIndex = indexToNode[w];
		assert(nodeStart[nodeIndex] == w && notInOrder[nodeIndex]);
		MatrixPosition pos;
		ScoreType maxValue = std::numeric_limits<ScoreType>::min();
		for (auto pair : RHelper)
		{
			auto v = pair.first;
			if (v == w) continue;
			auto scoreHere = pair.second - gapPenalty(distanceFromSeqToSeq(v, w, distanceMatrix));
			if (scoreHere > maxValue)
			{
				maxValue = scoreHere;
				pos = std::make_pair(v, j-1 + start);
			}
		}
		assert(maxValue >= -std::numeric_limits<ScoreType>::min() + 100);
		assert(maxValue <= std::numeric_limits<ScoreType>::max() - 100);
		return std::make_pair(maxValue, pos);
	}

	LengthType distanceFromSeqToSeq(LengthType start, LengthType end, const std::vector<std::vector<LengthType>>& distanceMatrix) const
	{
		auto startNode = indexToNode[start];
		auto endNode = indexToNode[end];
		return distanceMatrix[startNode][endNode] + end + nodeEnd[startNode] - nodeStart[endNode] - start;
	}

	std::vector<std::vector<LengthType>> getDistanceMatrix() const
	{
		//https://en.wikipedia.org/wiki/Floyd%E2%80%93Warshall_algorithm
		std::vector<std::vector<LengthType>> result;
		result.resize(inNeighbors.size());
		for (size_t i = 0; i < inNeighbors.size(); i++)
		{
			result[i].resize(inNeighbors.size(), nodeSequences.size()+1);
		}
		for (size_t i = 0; i < inNeighbors.size(); i++)
		{
			result[i][i] = 0;
			for (size_t j = 0; j < inNeighbors[i].size(); j++)
			{
				LengthType neighbor = inNeighbors[i][j];
				LengthType neighborLength = nodeEnd[neighbor] - nodeStart[neighbor];
				result[neighbor][i] = neighborLength;
			}
		}
		for (size_t k = 0; k < inNeighbors.size(); k++)
		{
			for (size_t i = 0; i < inNeighbors.size(); i++)
			{
				for (size_t j = 0; j < inNeighbors.size(); j++)
				{
					if (result[i][j] > result[i][k] + result[k][j]) 
					{
						result[i][j] = result[i][k] + result[k][j];
					}
				}
			}
		}
		return result;
	}
	
	ScoreType gapPenalty(LengthType length) const
	{
		if (length == 0) return 0;
		return gapStartPenalty + gapContinuePenalty * (length - 1);
	}
	
	ScoreType matchScore(char graph, char sequence) const
	{
		return graph == sequence ? 1 : -4;
	}

	std::vector<bool> notInOrder;
	std::vector<LengthType> nodeStart;
	std::vector<LengthType> nodeEnd;
	std::vector<LengthType> indexToNode;
	std::map<int, LengthType> nodeLookup;
	std::vector<int> nodeIDs;
	std::vector<std::vector<LengthType>> inNeighbors;
	std::string nodeSequences;
	ScoreType gapStartPenalty;
	ScoreType gapContinuePenalty;
};