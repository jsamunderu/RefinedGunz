#include "GlobalTypes.h"
#include "ArrayView.h"
#include "MFile.h"

#include "LauncherConfig.h"
#include "Hash.h"
#include "Sync.h"
#include "Log.h"

#include <algorithm>
#include <numeric>
#include <random>

#include <direct.h>

#include "TestAssert.h"

static std::mt19937 rng;

template <typename T>
static void FillRandom(T& c)
{
	std::uniform_int_distribution<> dist{ 0, UCHAR_MAX - 1 };
	for (u8& byte : c)
		byte = dist(rng);
}

static auto MakeBytes(size_t Size) {
	return std::vector<u8>(Size);
}

static auto MakeRandomBytes(size_t Size) {
	auto Bytes = MakeBytes(Size);
	FillRandom(Bytes);
	return Bytes;
}

static bool TestRollingHashMove()
{
	// Generate some data and check that, for every window in the sequence, the rolling hash
	// obtained by hashing the entire window at that position and the one gotten by updating
	// it incrementally is the same.

	// Since we are computing the new rolling hash from scratch at every window, this gives us
	// BlockSize * (SequenceSize - BlockSize) iterations of the innermost loop.
	// With a large BlockSize, this becomes way too large, so we use a relatively small number
	// instead of the one in LauncherConfig.
	constexpr auto BlockSize = 100;

	constexpr auto SequenceSize = BlockSize * 4;
	auto Sequence = MakeRandomBytes(SequenceSize);

	{
		Hash::Rolling IncrementalHash;
		IncrementalHash.HashMemory(Sequence.data(), BlockSize);

		// Start at i = 1 because so we have 1 byte to perform the incremental update with.
		for (int i = 1; i < SequenceSize - (BlockSize + 1); ++i)
		{
			Hash::Rolling NonincrementalHash;
			NonincrementalHash.HashMemory(Sequence.data() + i, BlockSize);

			IncrementalHash.Move(Sequence[i - 1], Sequence[i + BlockSize - 1], BlockSize);

			assert(IncrementalHash == NonincrementalHash);
		}
	}

	return true;
}

static bool TestRollingHashStream()
{
	constexpr auto SequenceSize = LauncherConfig::BlockSize;
	auto Sequence = MakeRandomBytes(SequenceSize);

	Hash::Rolling Nonincremental;
	Nonincremental.HashMemory(&Sequence[0], SequenceSize);

	// Check that a single stream Update call is equivalent to a single HashMemory call.
	{
		Hash::Rolling::Stream Stream;
		Stream.Update(&Sequence[0], SequenceSize);

		Hash::Rolling StreamHash;
		Stream.Final(StreamHash);
		assert(StreamHash == Nonincremental);
	}

	// Check that the hash obtained by hashing an entire block of memory is equivalent to the one
	// obtained by updating a stream with all of the memory.
	for (int i = 1; i < SequenceSize; ++i)
	{
		Hash::Rolling::Stream Stream;
		Stream.Update(&Sequence[0], i);
		Stream.Update(&Sequence[i], SequenceSize - i);

		Hash::Rolling Incremental;
		Stream.Final(Incremental);

		assert(Nonincremental == Incremental);
	}

	return true;
}

static bool TestRollingHash()
{
	assert(TestRollingHashMove());
	assert(TestRollingHashStream());

	return true;
}

static void WriteFile(const char* Path, const void* Buffer, size_t Size)
{
	CreateDirectoriesIfNonexistent(Path);

	MFile::RWFile File{ Path, MFile::ClearExistingContents };
	assert(!File.error());

	File.write(Buffer, Size);
	assert(!File.error());
}

static bool TestFile(const ArrayView<u8>& SrcFileContents,
	const ArrayView<u8>& DestFileContents,
	size_t ExpectedNumUnmatchingBlocks)
{
	constexpr auto SrcFilePath = "temp/src.dat";
	constexpr auto DestFilePath = "temp/dest.dat";

	WriteFile(SrcFilePath, SrcFileContents.data(), SrcFileContents.size());
	WriteFile(DestFilePath, DestFileContents.data(), DestFileContents.size());

	char SyncPath[MFile::MaxPath];
	sprintf_safe(SyncPath, "%s.sync", SrcFilePath);

	assert(Sync::MakeSyncFile(SyncPath, SrcFilePath));

	// Instead of starting a local webserver, just use FILE URIs.
	char cwd[MFile::MaxPath];
	_getcwd(cwd, sizeof(cwd));

	auto MakeFileURI = [&](auto&& Dest, auto&& Src) {
		sprintf_safe(Dest, "file:///%s/%s", cwd, Src);
	};

	char SrcURL[MFile::MaxPath];
	MakeFileURI(SrcURL, SrcFilePath);
	char SyncURL[MFile::MaxPath];
	MakeFileURI(SyncURL, SyncPath);

	auto DownloadManager = CreateDownloadManager();
	assert(bool(DownloadManager));

	Hash::Strong ExpectedHash;
	ExpectedHash.HashFile(SrcFilePath);

	const auto ExpectedSize = SrcFileContents.size();

	Hash::Strong ActualHash;
	u64 ActualSize;
	Sync::BlockCounts Counts;

	assert(Sync::SynchronizeFile(DestFilePath, SrcURL, SyncURL, SrcFileContents.size(),
		DownloadManager, &ActualHash, &ActualSize, &Counts));

	assert(Counts.UnmatchingBlocks == ExpectedNumUnmatchingBlocks);

	assert(ActualHash == ExpectedHash);
	assert(ActualSize == ExpectedSize);

	ActualHash.HashFile(DestFilePath);
	ActualSize = MFile::GetAttributes(DestFilePath)->Size;

	assert(ActualHash == ExpectedHash);
	assert(ActualSize == ExpectedSize);

	return true;
}

template <size_t FileSize>
static bool TestSize()
{
	auto MakeView = [&](auto&& c) { return ArrayView<u8>{c.data(), c.size()}; };

	auto FileBuffer = MakeRandomBytes(FileSize);

	// Test that the sync algorithm can recognize that two files are the same.
	auto View = MakeView(FileBuffer);
	assert(TestFile(View, View, 0));

	// This performs a copy.
	auto ModifiedBuffer = FileBuffer;

	const auto OldFirstByte = ModifiedBuffer[0];

	// Change the first byte.
	ModifiedBuffer[0] += 128;

	assert(TestFile(MakeView(FileBuffer), MakeView(ModifiedBuffer), 1));

	// Check that it can recognize that only one block differs between FileBuffer and FileBuffer
	// from the first byte to the end.
	assert(TestFile(MakeView(FileBuffer),
		ArrayView<u8>{FileBuffer.data() + 1, FileBuffer.size() - 1},
		1));

	ModifiedBuffer[0] = OldFirstByte;

	constexpr auto NumBlocks = Sync::detail::ceildiv(FileSize, LauncherConfig::BlockSize);

	// Alter the first byte of every block but the last.
	for (int i = 0; i < NumBlocks - 1; ++i)
	{
		ModifiedBuffer[i * LauncherConfig::BlockSize] += 42;
	}

	// All blocks but the last should be modified.
	assert(TestFile(MakeView(FileBuffer),
		MakeView(ModifiedBuffer),
		NumBlocks - 1));

	// Undo every second alteration, so that (NumBlocks - 1) / 2 blocks will be modified.
	for (int i = 0; i < NumBlocks - 1; i += 2)
	{
		ModifiedBuffer[i * LauncherConfig::BlockSize] -= 42;
	}

	assert(TestFile(MakeView(FileBuffer),
		MakeView(ModifiedBuffer),
		(NumBlocks - 1) / 2));

	return true;
}

static bool TestSyncFile()
{
	TestSize<1>(); // 1 byte
	TestSize<72>(); // 72 bytes
	TestSize<LauncherConfig::BlockSize / 2>(); // Half a block
	TestSize<100 * 1024>(); // 100 KiB
	TestSize<LauncherConfig::BlockSize * 5>(); // 5 blocks
	TestSize<5 * 1024 * 1024>(); // 5 MiB
	TestSize<60 * 1024 * 1024>(); // 60 MiB

	return true;
}

bool TestLauncher()
{
	assert(TestRollingHash());
	assert(TestSyncFile());

	return true;
}