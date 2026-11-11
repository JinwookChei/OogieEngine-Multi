#pragma once
#include <vector>
#include <list>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <stack>
#include <string>
#include <functional>
#include "Memory.h"

/*
		[Container]
		서버 코드에서 쓰는 STL 컨테이너 별칭. std:: 컨테이너를 직접 쓰지 않고 이 이름을 쓴다.
		- 모든 컨테이너는 StlAllocator → DefaultAllocator(Debug: Stomp / Release: Pool)에서 메모리를 받는다.
		- 별칭은 이름만 짧게 하는 것이라 실행 비용이 없다.
		- Queue / Stack / PriorityQueue는 할당기를 직접 받지 않는다.
		  안쪽에 담는 컨테이너(Deque / Vector)가 메모리를 관리하므로, 그 안쪽 컨테이너를 우리 별칭으로 바꿔 준다.
		- StlAllocator와 같이 CoreGlobal이 살아 있는 동안에만 쓴다 (Release는 GMemory를 쓴다).
*/

/*
	[순서 컨테이너]
*/
template<typename Type>
using Vector = std::vector<Type, StlAllocator<Type>>;

template<typename Type>
using List = std::list<Type, StlAllocator<Type>>;

template<typename Type>
using Deque = std::deque<Type, StlAllocator<Type>>;

/*
	[연관 컨테이너] 트리 기반, 키 순서로 정렬
	Map의 원소 타입은 std::pair<const Key, Type>이다. 할당기 타입도 똑같이 맞춰야 한다 (const를 빼면 컴파일 오류).
*/
template<typename Key, typename Type, typename Pred = std::less<Key>>
using Map = std::map<Key, Type, Pred, StlAllocator<std::pair<const Key, Type>>>;

template<typename Key, typename Pred = std::less<Key>>
using Set = std::set<Key, Pred, StlAllocator<Key>>;

/*
	[해시 컨테이너] 해시 테이블 기반, 순서 없음
*/
template<typename Key, typename Type, typename Hasher = std::hash<Key>, typename KeyEq = std::equal_to<Key>>
using HashMap = std::unordered_map<Key, Type, Hasher, KeyEq, StlAllocator<std::pair<const Key, Type>>>;

template<typename Key, typename Hasher = std::hash<Key>, typename KeyEq = std::equal_to<Key>>
using HashSet = std::unordered_set<Key, Hasher, KeyEq, StlAllocator<Key>>;

/*
	[어댑터] 안쪽 컨테이너의 기본값을 우리 별칭으로 바꾼다 (Deque / Vector가 위에 먼저 선언되어 있어야 한다)
*/
template<typename Type, typename Container = Deque<Type>>
using Queue = std::queue<Type, Container>;

template<typename Type, typename Container = Deque<Type>>
using Stack = std::stack<Type, Container>;

// Pred 기본값 std::less → 큰 값이 먼저 나온다 (std::priority_queue와 같다)
template<typename Type, typename Container = Vector<Type>, typename Pred = std::less<typename Container::value_type>>
using PriorityQueue = std::priority_queue<Type, Container, Pred>;

/*
	[문자열]
*/
using String = std::basic_string<char, std::char_traits<char>, StlAllocator<char>>;
using WString = std::basic_string<wchar_t, std::char_traits<wchar_t>, StlAllocator<wchar_t>>;
