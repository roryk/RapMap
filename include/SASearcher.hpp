#ifndef SA_SEARCHER_HPP
#define SA_SEARCHER_HPP

#include <vector>
#include <algorithm>
#include <iterator>
#include "jellyfish/mer_dna.hpp"

#include "RapMapUtils.hpp"
#include "RapMapSAIndex.hpp"

template <typename RapMapIndexT>
class SASearcher {
    public:
        using OffsetT = typename RapMapIndexT::IndexType;

        SASearcher(RapMapIndexT* rmi) :
            rmi_(rmi), seq_(&rmi->seq), sa_(&rmi->SA) {}

        int cmp(std::string::iterator abeg,
                std::string::iterator aend,
                std::string::iterator bbeg,
                std::string::iterator bend) {
            auto ait = abeg;
            auto bit = bbeg;
            //size_t la = a.length();
            //size_t lb = b.length();
            while (ait < aend and bit < bend) {
                if (*ait < *bit) {
                    return -1;
                } else if (*ait > *bit) {
                    return 1;
                }
                ++ait;
                ++bit;
            }
            if (bit == bend and ait < aend) {
                return 1;
            }
            return 0;
        }

        enum class SearchDirection : uint8_t {
            UP = 0, DOWN
        };
        struct BoundSearchResult {
            int maxLen;
            int bound;
            SearchDirection dir;
        };



	/**
	 * OK!  It should be (is) possible to figure out what we need with only two binary
	 * searches.  However, that seems to have some tricky corner cases and has been
	 * somewhat illusive so far.  This "naive" version performs *3* binary searches.
	 * The first determines the length of the maximum mappable prefix (MMP).  The second
	 * finds the lower bound for the query interval and the third finds the upper bound.
	 * The final binary search *is* optimized (it has a lower bound given by the value)
	 * returned by second search.  However, this method is likely a bit slower than the
	 * one above (when it can be made to work correctly at all times).
	 */
        template <typename IteratorT>
        std::tuple<OffsetT, OffsetT, OffsetT> extendSearchNaive(
                OffsetT lbIn, // The lower bound for the search
                OffsetT ubIn, // The upper bound for the search
                OffsetT startAt, // The offset at which to start looking
                IteratorT qb, // Iterator to the beginning of the query
                IteratorT qe, // Iterator to the end of the query
                bool complementBases=false // True if bases should be complemented
                                           // before comparison
                ) {

            std::vector<OffsetT>& SA = *sa_;
            std::string& seq = *seq_;

            OffsetT m = std::distance(qb, qe);
            size_t n = seq.length();

            auto sb = seq.begin();
            auto se = seq.end();

            // If the bounds are already trivial, just figure how long
            // of a prefix we share and return the interval.
            if (ubIn - lbIn == 2) {
                lbIn += 1;
                auto i = startAt;
                while (i < m and SA[lbIn] + i < n) {
                    char queryChar = ::toupper(*(qb + i));
                    // If we're reverse complementing
                    if (complementBases) {
                        queryChar = rapmap::utils::my_mer::complement(queryChar);
                    }
                    if ( queryChar < *(sb + SA[lbIn] + i) ) {
                        break;
                    } else if ( queryChar > *(sb + SA[lbIn] + i)) {
                        break;
                    }
                    ++i;
                }
                return std::make_tuple(lbIn, ubIn, i);
            }

            BoundSearchResult res1, res2;

            char smallest = '#';
            char largest = '}';
            char sentinel = smallest;

            OffsetT l = lbIn, r = ubIn;
            OffsetT lcpLP = startAt, lcpRP = startAt;
            OffsetT c{0};
            OffsetT i{0};

            OffsetT maxI{startAt};
            OffsetT prevI = startAt;
            OffsetT prevILow = startAt;
            OffsetT prevIHigh = startAt;
            OffsetT validBoundLow = ubIn;
            OffsetT validBoundHigh = lbIn;
            OffsetT validBound = 0;
            bool plt{true};
            // Reduce the search interval until we hit a border
            // i.e. until c == r - 1 or c == l + 1
            while (true) {
                c = (l + r) / 2;
                plt = true;
                i = std::min(lcpLP, lcpRP);
                while (i < m and SA[c] + i < n) {
                    char queryChar = ::toupper(*(qb + i));
                    // If we're reverse complementing
                    if (complementBases) {
                        queryChar = rapmap::utils::my_mer::complement(queryChar);
                    }

                    if ( queryChar < *(sb + SA[c] + i) ) {
                        if (i > prevIHigh) {
                            prevIHigh = i;
                            validBoundHigh = c;
                        } else if (i == prevIHigh) {
                            validBoundHigh = c < validBoundHigh ? c : validBoundHigh;
                        }

                        break;
                    } else if ( queryChar > *(sb + SA[c] + i)) {
                        if (i > prevILow) {
                            prevILow = i;
                            validBoundLow = c;
                        } else if (i == prevILow) {
                            validBoundLow = c > validBoundLow ? c : validBoundLow;
                        }
                        plt = false;
                        break;
                    }

                    ++i;
                }
                if (i == m or SA[c] + i == n) {
                    if (i > prevIHigh) {
                        prevIHigh = i;
                        validBoundHigh = c;
                    } else if (i == prevIHigh) {
                        validBoundHigh = c < validBoundHigh ? c : validBoundHigh;
                    }
                }

                if (plt) {
                    if (c == l + 1) {
                        auto maxI = std::max(std::max(i, prevILow), prevIHigh);
                        res1.maxLen = maxI;
                        break;
                    }
                    r = c;
                    lcpRP = i;
                } else {
                    if (c == r - 1) {
                        maxI = std::max(std::max(i, prevILow), prevIHigh);
                        res1.maxLen = maxI;
                        break;
                    }
                    l = c;
                    lcpLP = i;
                }
            }

            bool knownValid{true};
            m = res1.maxLen + 1;

            // first search for the lower bound
            sentinel = '#';
            l = lbIn;
            r = ubIn;

            lcpLP = startAt;
            lcpRP = startAt;
            c = 0;
            plt = true;
            i = startAt;
            while (true) {
                c = (l + r) / 2;
                plt = true;
                i = std::min(lcpLP, lcpRP);
                while (i < m and SA[c] + i < n) {
                    char queryChar = (i < m - 1) ? ::toupper(*(qb + i)) : sentinel;
                    // If we're reverse complementing
                    if (queryChar != sentinel and complementBases) {
                        queryChar = rapmap::utils::my_mer::complement(queryChar);
                    }

                    if ( queryChar < *(sb + SA[c] + i) ) {
                     	break;
                    } else if ( queryChar > *(sb + SA[c] + i)) {
                        plt = false;
                        break;
                    }
                    ++i;
                }
                if (plt) {
                    if (c == l + 1) {
                        res1.bound = c;
                        break;
                    }
                    r = c;
                    lcpRP = i;
                } else {
                    if (c == r - 1) {
                        res1.bound = r;
                        break;
                    }
                    l = c;
                    lcpLP = i;
                }
            }

            // then search for the upper bound
            sentinel = '{';
            l = res1.bound - 1;
            r = ubIn;

            lcpLP = startAt;
            lcpRP = startAt;
            c = 0;
            plt = true;
            i = startAt;
            while (true) {
                c = (l + r) / 2;
                plt = true;
                i = std::min(lcpLP, lcpRP);
                while (i < m and SA[c] + i < n) {
                    char queryChar = (i < m - 1) ? ::toupper(*(qb + i)) : sentinel;
                    // If we're reverse complementing
                    if (queryChar != sentinel and complementBases) {
                        queryChar = rapmap::utils::my_mer::complement(queryChar);
                    }

                    if ( queryChar < *(sb + SA[c] + i) ) {
                     	break;
                    } else if ( queryChar > *(sb + SA[c] + i)) {
                        plt = false;
                        break;
                    }
                    ++i;
                }
                if (plt) {
                    if (c == l + 1) {
                        res2.bound = c;
                        break;
                    }
                    r = c;
                    lcpRP = i;
                } else {
                    if (c == r - 1) {
                        res2.bound = r;
                        break;
                    }
                    l = c;
                    lcpLP = i;
                }
            }

            // Must occur at least once!
            if (res1.bound == res2.bound) { res2.bound += 1; }
            return std::make_tuple(res1.bound, res2.bound, res1.maxLen);
        }


        /**
         * Compute the longest common extension between the suffixes
         * at T[SA[p1]] and T[SA[p2]].  Start the comparison at `startAt`
         * positions into the suffix, and only consider an extension
         * going to at most position `stopAt`.
         */
        OffsetT lce(OffsetT p1, OffsetT p2,
                    OffsetT startAt=0,
                    OffsetT stopAt=std::numeric_limits<OffsetT>::max(),
                    bool verbose=false) {
            std::string& seq = *seq_;
            std::vector<OffsetT>& SA = *sa_;
            OffsetT len = static_cast<OffsetT>(startAt);
            auto o1 = SA[p1] + startAt;
            auto o2 = SA[p2] + startAt;
            auto maxIndex = std::max(o1, o2);
            while (maxIndex + len < textLen_ and seq[o1+len] == seq[o2+len]) {
                if (seq[o1+len] == '$') { break; }
                if (len >= stopAt) { break; }
                ++len;
            }
            return len;
        }

    private:
        RapMapIndexT* rmi_;
        std::string* seq_;
        std::vector<OffsetT>* sa_;
        OffsetT textLen_;
};


        /*
        // http://www.cs.jhu.edu/~langmea/resources/lecture_notes/suffix_arrays.pdf
        std::tuple<int, int> querySimpleAccel(std::string::iterator qb,
                                              std::string::iterator qe) {
            std::vector<int>& SA = *sa_;
            std::string& seq = *seq_;
            //ForwardIt it;
            auto sb = seq.begin();
            auto se = seq.end();

            size_t n = seq.length();
            size_t m = std::distance(qb, qe);
            size_t l = 0, r = n;
            size_t lcpLP = 0, lcpRP = 0;
            size_t c{0};
            size_t i{0};
            bool plt{true};
            size_t lower{0};
            while (true) {
                c = (l + r) / 2;
                plt = true;
                i = std::min(lcpLP, lcpRP);
                while (i < m and SA[c] + i < n) {
                    if ( *(qb + i) < *(sb + SA[c] + i) ) {
                        break;
                    } else if ( *(qb + i) > *(sb + SA[c] + i)) {
                        plt = false;
                        break;
                    }
                    ++i;
                }
                if (plt) {
                    if (c == l + 1) { lower = c; break; }
                    r = c;
                    lcpRP = i;
                } else {
                    if (c == r - 1) { lower = r; break; }
                    l = c;
                    lcpLP = i;
                }
            }

            i = 0;
            l = 0;
            r = n;
            lcpLP = 0;
            lcpRP = 0;
            size_t upper{0};
            while (true) {
                c = (l + r) / 2;
                plt = true;
                i = std::min(lcpLP, lcpRP);
                while (i < m and SA[c] + i < n) {
                    if ( *(qb + i) < *(sb + SA[c] + i) ) {
                        break;
                    } else if ( *(qb + i) > *(sb + SA[c] + i)) {
                        plt = false;
                        break;
                    }
                    ++i;
                }
                if (plt) {
                    if (c == l + 1) { upper = c; break; }
                    r = c;
                    lcpRP = i;
                } else {
                    if (c == r - 1) { upper = r; break; }
                    l = c;
                    lcpLP = i;
                }
            }
            return std::make_tuple(lower, upper);
        }


        ''
        // http://www.cs.jhu.edu/~langmea/resources/lecture_notes/suffix_arrays.pdf
        // templated on the iterator type so we can use a forward or revers iterator
        template <typename IteratorT>
        std::tuple<int, int, int> extendSearch(
                int lbIn, // The lower bound for the search
                int ubIn, // The upper bound for the search
                int startAt, // The offset at which to start looking
                IteratorT qb, // Iterator to the beginning of the query
                IteratorT qe, // Iterator to the end of the query
                bool complementBases=false // True if bases should be complemented
                                           // before comparison
                ) {

            std::vector<int>& SA = *sa_;
            std::string& seq = *seq_;

            int m = std::distance(qb, qe);
            size_t n = seq.length();

            auto sb = seq.begin();
            auto se = seq.end();

            // If the bounds are already trivial, just figure how long
            // of a prefix we share and return the interval.
            if (ubIn - lbIn == 2) {
                lbIn += 1;
                auto i = startAt;
                while (i < m and SA[lbIn] + i < n) {
                    char queryChar = ::toupper(*(qb + i));
                    // If we're reverse complementing
                    if (complementBases) {
                        queryChar = rapmap::utils::my_mer::complement(queryChar);
                    }
                    if ( queryChar < *(sb + SA[lbIn] + i) ) {
                        break;
                    } else if ( queryChar > *(sb + SA[lbIn] + i)) {
                        break;
                    }
                    ++i;
                }
                return std::make_tuple(lbIn, ubIn, i);
            }

            BoundSearchResult res1, res2;

            char smallest = '#';
            char largest = '}';
            char sentinel = smallest;

            int l = lbIn, r = ubIn;
            int lcpLP = startAt, lcpRP = startAt;
            int c{0};
            int i{0};
            int maxI{startAt};
            int prevI = startAt;
            int prevILow = startAt;
            int prevIHigh = startAt;
            int validBoundLow = ubIn;
            int validBoundHigh = lbIn;
            int validBound = 0;
            bool plt{true};
            bool prevPLT{true};
            //std::cerr << "lbIn = " << lbIn << ", ubIn = " << ubIn << "\n";
            // Reduce the search interval until we hit a border
            // i.e. until c == r - 1 or c == l + 1
            while (true) {
                c = (l + r) / 2;
                //std::cerr << "l = " << l << ", r = " << r << ", c = " << c << '\n';
                plt = true;
                i = std::min(lcpLP, lcpRP);
                while (i < m and SA[c] + i < n) {
                    char queryChar = ::toupper(*(qb + i));
                    // If we're reverse complementing
                    if (complementBases) {
                        queryChar = rapmap::utils::my_mer::complement(queryChar);
                    }

                    if ( queryChar < *(sb + SA[c] + i) ) {
                        if (i > prevIHigh) {
                            prevIHigh = i;
                            validBoundHigh = c;
                        } else if (i == prevIHigh) {
                            validBoundHigh = c < validBoundHigh ? c : validBoundHigh;
                        }
                        //std::cerr << "(l = " << l << ", r = " << r << ") pattern < SA[" << c << "]\n";
                        //std::cerr << "(i = " << i << ", m = " << m << ") " << queryChar << " < " <<  *(sb + SA[c] + i) << "\n";

                        break;
                    } else if ( queryChar > *(sb + SA[c] + i)) {
                        if (i > prevILow) {
                            prevILow = i;
                            validBoundLow = c;
                        } else if (i == prevILow) {
                            validBoundLow = c > validBoundLow ? c : validBoundLow;
                        }
                        //std::cerr << "(l = " << l << ", r = " << r << ") pattern > SA[" << c << "]\n";
                        //std::cerr << "(i = " << i << ", m = " << m << ") " << queryChar << " > " <<  *(sb + SA[c] + i) << "\n";
                        plt = false;
                        break;
                    }

                    ++i;
		}
		if (i == m or SA[c] + i == n) {
			if (i > prevIHigh) {
				prevIHigh = i;
				validBoundHigh = c;
			} else if (i == prevIHigh) {
				validBoundHigh = c < validBoundHigh ? c : validBoundHigh;
			}
		}

                if (plt) {
                    if (c == l + 1) {
                        std::cerr << "path 1\n";
                        auto maxI = std::max(std::max(i, prevILow), prevIHigh);
                        res1.maxLen = maxI;
                        if (maxI == m) {
                            res1.dir = SearchDirection::DOWN;
                            res1.bound = c;
                        } else {
                            validBound = (prevILow >= prevIHigh) ? validBoundLow : validBoundHigh;
                            res1.bound = validBound;
                            res1.dir = (res1.bound == validBoundLow) ? SearchDirection::DOWN : SearchDirection::UP;
                        }
                        break;
                    }
                    r = c;
                    lcpRP = i;
                } else {
                    if (c == r - 1) {
                        std::cerr << "path 2\n";
                        maxI = std::max(std::max(i, prevILow), prevIHigh);
                        res1.maxLen = maxI;
                        validBound = (prevILow >= prevIHigh) ? validBoundLow : validBoundHigh;
                        if (maxI == m) {
                            res1.bound = r;
                        } else {
                            res1.bound = validBound;
                        }
                        res1.dir = (res1.bound == validBoundLow) ? SearchDirection::DOWN : SearchDirection::UP;
                        break;
                    }
                    l = c;
                    lcpLP = i;
                }
            }


            bool knownValid{true};
            m = res1.maxLen + 1;

            switch (res1.dir) {
                case SearchDirection::UP:
                    sentinel = '#';
                    r = res1.bound;
                    l = lbIn;
                    std::cerr << "direction was UP; lb = " << l << ", ub = " << r << "\n";
                    std::cerr << "direction was UP; origLb = " << lbIn << ", origUb = " << ubIn << "\n";
                    break;
                case SearchDirection::DOWN:
                    sentinel = '{';
                    r = ubIn;
                    l = res1.bound;
                    std::cerr << "direction was DOWN; lb = " << l << ", ub = " << r << "\n";
                    std::cerr << "direction was UP; origLb = " << lbIn << ", origUb = " << ubIn << "\n";
                    break;
            }

            if (r - l < 2) {
                if (r == l) { r += 1; }
                //std::cerr << "early exit!\n";
                return std::make_tuple(l, r, res1.maxLen);
            }


            lcpLP = startAt;
            lcpRP = startAt;
            c = 0;
            plt = true;
            prevPLT = true;
            prevI = 0;
            prevILow = 0;
            prevIHigh = 0;
            i = startAt;
            validBound = 0;
            validBoundLow = ubIn;
            validBoundHigh = lbIn;
            while (true) {
                c = (l + r) / 2;
                plt = true;
                i = std::min(lcpLP, lcpRP);
                while (i < m and SA[c] + i < n) {
                    char queryChar = (i < m - 1) ? ::toupper(*(qb + i)) : sentinel;
                    // If we're reverse complementing
                    if (queryChar != sentinel and complementBases) {
                        queryChar = rapmap::utils::my_mer::complement(queryChar);
                    }

                    if ( queryChar < *(sb + SA[c] + i) ) {
                     	break;
                    } else if ( queryChar > *(sb + SA[c] + i)) {
                        plt = false;
                        break;
                    }
                    ++i;
                }
                if (plt) {
                    if (c == l + 1) {
                        res2.dir = SearchDirection::DOWN;
                        res2.bound = c;
                        break;
                    }
                    r = c;
                    lcpRP = i;
                } else {
                    if (c == r - 1) {
                        res2.bound = r;
                        break;
                    }
                    l = c;
                    lcpLP = i;
                }
            }

            auto bound1 = std::min(res1.bound, res2.bound);
            auto bound2 = std::max(res1.bound, res2.bound);
            // Must occur at least once!
            if (bound1 == bound2) { bound2 += 1; }
            return std::make_tuple(bound1, bound2, res1.maxLen);
        }
        */

#endif //SA_SEARCHER_HPP
