#pragma once
#include <complex>
#include <memory>
#include <vector>

//nodal analysis, the core every simulated medium shares: solves  G x = b  for a network of nodes joined
//by branches, plus shunts from nodes to the reference (ground), with b the flow injected into each node.
//  electrical (AC): x = voltage phasors, G = admittances, b = currents          T = std::complex<double>
//  thermal (later): x = temperatures, G = thermal conductances, b = heat flows   T = double
//  liquid  (later): x = pressures, G = flow conductances, b = pump flows         T = double
//build G with addBranch/addShunt, factorise() once, then solve() as often as b changes: each solve reuses
//the factorisation, so it is far cheaper than the first. G must be rebuilt and refactorised when it changes.
//
//the Eigen code is all in nodalSolver.cpp, which is built optimised even in Debug (see cmake/optimised),
//because Eigen's templates run 15-40x slower unoptimised. so this header doesn't include Eigen.
template <typename T>
class nodalSolver
{
public:
	using vector = std::vector<T>;

	nodalSolver();
	~nodalSolver();
	nodalSolver(const nodalSolver&) = delete;
	nodalSolver& operator=(const nodalSolver&) = delete;

	//start a new matrix of _nodeCount nodes, all unconnected
	void reset(int _nodeCount);
	int nodeCount() const;

	//a branch of admittance _g between nodes _a and _b
	void addBranch(int _a, int _b, T _g);
	//an admittance _g from node _n to the reference
	void addShunt(int _n, T _g);
	//adds _value to one entry of G. for equations that aren't plain networks (e.g. heat carried one way by a
	//flowing liquid makes G unsymmetric); repeated entries add up
	void addEntry(int _row, int _column, T _value);

	//factorises G. false if it is singular (some part of the network has no path to the reference)
	bool factorise();
	bool isFactorised() const;

	//solves for x given the injections b (one per node). false if not factorised or the solve failed
	bool solve(const vector& _injections, vector& _x) const;

private:
	struct implementation;
	std::unique_ptr<implementation> data;
};

extern template class nodalSolver<double>;
extern template class nodalSolver<std::complex<double>>;
