// Reads or writes a whole world to a save file with the same code: each thing to save is listed once
// in a Serialize(Archive&) function, and the archive either fills it in or writes it out.
//
// Save files carry a version. When a later build adds something to the world, it bumps kSaveVersion
// and reads the new field only from saves at least that new (Ar.Version >= N), so older saves still
// load and the new field keeps its default.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace earth
{

inline constexpr uint32_t kSaveVersion = 11; // 2: inventions and words for things. 3: beliefs. 4: names, trust and leaders. 5: remembered places. 6: grief and kindness. 7: stored food. 8: other kinds of human. 9: kept herds and crops. 10: when each skill became findable. 11: the sky (orbit ice, disaster winters, blasts).

class Archive
{
public:
	// Opens Path to write (Writing true) or read. Check Ok() afterwards and after use.
	Archive(const std::string& Path, bool InWriting) : Writing(InWriting)
	{
		File = std::fopen(Path.c_str(), Writing ? "wb" : "rb");
		Good = File != nullptr;
		uint32_t Magic = 0x4B353133; // "315K"
		Io(Magic);
		Good = Good && Magic == 0x4B353133;
		Version = kSaveVersion;
		Io(Version);
		Good = Good && Version <= kSaveVersion;
	}
	~Archive()
	{
		if (File) std::fclose(File);
	}
	Archive(const Archive&) = delete;
	Archive& operator=(const Archive&) = delete;

	bool Ok() const { return Good; }
	bool IsWriting() const { return Writing; }
	bool IsReading() const { return !Writing; }
	uint32_t Version = kSaveVersion; // Of the file being read; kSaveVersion when writing.

	template <class T>
	std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T>> Io(T& V)
	{
		Bytes(&V, sizeof(T));
	}

	void Io(std::string& S)
	{
		uint64_t N = S.size();
		Io(N);
		if (!Writing) S.resize(Good ? static_cast<size_t>(N) : 0);
		if (N) Bytes(S.data(), S.size());
	}

	void Io(std::vector<bool>& V)
	{
		uint64_t N = V.size();
		Io(N);
		if (!Writing) V.assign(Good ? static_cast<size_t>(N) : 0, false);
		for (size_t i = 0; i < V.size(); ++i)
		{
			bool B = V[i];
			Io(B);
			V[i] = B;
		}
	}

	template <class T>
	void Io(std::vector<T>& V)
	{
		uint64_t N = V.size();
		Io(N);
		if (!Writing) V.resize(Good ? static_cast<size_t>(N) : 0);
		if constexpr (std::is_arithmetic_v<T>)
		{
			if (!V.empty()) Bytes(V.data(), V.size() * sizeof(T));
		}
		else
		{
			for (T& E : V) Io(E);
		}
	}

	template <class T, size_t N>
	void Io(std::array<T, N>& A)
	{
		for (T& E : A) Io(E);
	}

	template <class A, class B>
	void Io(std::pair<A, B>& P)
	{
		Io(P.first);
		Io(P.second);
	}

	template <class K, class V>
	void Io(std::unordered_map<K, V>& M)
	{
		uint64_t N = M.size();
		Io(N);
		if (Writing)
		{
			for (auto& [Key, Value] : M)
			{
				K Copy = Key;
				Io(Copy);
				Io(Value);
			}
		}
		else
		{
			M.clear();
			for (uint64_t i = 0; i < N && Good; ++i)
			{
				K Key{};
				Io(Key);
				Io(M[Key]);
			}
		}
	}

	// Things shared by many owners, such as one genome shared by everyone born from it, are saved
	// once and shared again on loading.
	template <class T>
	void Io(std::shared_ptr<const T>& P)
	{
		int64_t Index = -1;
		if (Writing)
		{
			if (P)
			{
				auto It = SharedIndex.find(P.get());
				Index = It != SharedIndex.end() ? It->second : static_cast<int64_t>(SharedIndex.size());
				const bool New = It == SharedIndex.end();
				if (New) SharedIndex[P.get()] = Index;
				Io(Index);
				if (New)
				{
					T Copy = *P;
					Io(Copy);
				}
				return;
			}
			Io(Index);
			return;
		}
		Io(Index);
		if (Index < 0 || !Good)
		{
			P.reset();
			return;
		}
		if (Index == static_cast<int64_t>(Shared.size()))
		{
			auto Made = std::make_shared<T>();
			Io(*Made);
			Shared.push_back(Made);
		}
		P = Index < static_cast<int64_t>(Shared.size()) ? std::static_pointer_cast<const T>(Shared[static_cast<size_t>(Index)]) : nullptr;
		Good = Good && P != nullptr;
	}

	// Anything else lists its own fields.
	template <class T>
	std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T>> Io(T& V)
	{
		V.Serialize(*this);
	}

private:
	void Bytes(void* Data, size_t Size)
	{
		if (!Good) return;
		Good = (Writing ? std::fwrite(Data, 1, Size, File) : std::fread(Data, 1, Size, File)) == Size;
	}

	std::FILE* File = nullptr;
	bool Writing = false;
	bool Good = false;
	std::unordered_map<const void*, int64_t> SharedIndex;
	std::vector<std::shared_ptr<const void>> Shared;
};

} // namespace earth
