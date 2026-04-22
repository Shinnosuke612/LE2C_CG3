#pragma once
#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")

#include <fstream>
#include <cassert>
#include <cstdint>
#include <wrl.h>

class Audio{
public:
	// チャンクヘッダ
	struct ChunkHeader{
		char id[4];
		int32_t size;
	};

	// RIFFヘッダチャンク
	struct RiffHeader{
		ChunkHeader chunk;
		char type[4];
	};

	// fmtチャンク
	struct FormatChunk{
		ChunkHeader chunk;
		WAVEFORMATEX fmt;
	};

	// 音声データ
	struct SoundData{
		// 波形フォーマット
		WAVEFORMATEX wfex{};
		// バッファの先頭アドレス
		BYTE* pBuffer = nullptr;
		// バッファのサイズ
		unsigned int bufferSize = 0;
	};

public:
	// 初期化
	void Initialize();

	// 終了処理
	void Finalize();

	// wav読み込み
	SoundData SoundLoadWave(const char* filename);

	// wav再生
	void SoundPlayWave(const SoundData& soundData);

	// wav解放
	void SoundUnload(SoundData* soundData);

private:
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masterVoice_ = nullptr;
};