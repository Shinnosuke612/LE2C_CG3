// 役割: XAudio2を使ったWAVデータの再生処理を実装する。
#include "Audio.h"

Audio* Audio::instance = nullptr;

Audio* Audio::GetInstance() {
	return instance;
}

void Audio::Initialize(){
	instance = this;
	HRESULT result;

	// XAudioエンジンのインスタンスを生成
	result = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(result));

	// マスターボイスを生成
	result = xAudio2_->CreateMasteringVoice(&masterVoice_);
	assert(SUCCEEDED(result));
}

void Audio::Finalize(){
	// masterVoiceはxAudio2解放時に無効になる
	masterVoice_ = nullptr;
	xAudio2_.Reset();
	instance = nullptr;
}

Audio::SoundData Audio::SoundLoadWave(const char* filename){
	HRESULT result;
	(void)result;

	// 1. ファイルオープン
	std::ifstream file;
	file.open(filename, std::ios_base::binary);
	assert(file.is_open());

	// RIFFヘッダの読み込み
	RiffHeader riff;
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
	assert(file.good());

	// "RIFF"チェック
	assert(std::strncmp(riff.chunk.id, "RIFF", 4) == 0);
	// "WAVE"チェック
	assert(std::strncmp(riff.type, "WAVE", 4) == 0);

	// fmtチャンクの読み込み
	FormatChunk format = {};
	file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));
	assert(file.good());

	// fmtが来るまで読み飛ばす
	while(std::strncmp(format.chunk.id, "fmt ", 4) != 0){
		file.seekg(format.chunk.size, std::ios_base::cur);
		file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));
		assert(file.good());
	}

	// fmt本体読み込み
	assert(format.chunk.size <= sizeof(format.fmt));
	file.read(reinterpret_cast<char*>(&format.fmt), format.chunk.size);
	assert(file.good());

	// dataチャンクを探す
	ChunkHeader data = {};
	file.read(reinterpret_cast<char*>(&data), sizeof(data));
	assert(file.good());

	while(std::strncmp(data.id, "data", 4) != 0){
		file.seekg(data.size, std::ios_base::cur);
		file.read(reinterpret_cast<char*>(&data), sizeof(data));
		assert(file.good());
	}

	// dataチャンクのデータ部
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);
	assert(file.good());

	// ファイルクローズ
	file.close();

	// returnする音声データ
	SoundData soundData = {};
	soundData.wfex = format.fmt;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;

	return soundData;
}

void Audio::SoundPlayWave(const SoundData& soundData){
	HRESULT result;

	// 波形フォーマットを元にSourceVoiceを生成
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2_->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result));

	// 再生する波形データの設定
	XAUDIO2_BUFFER buf{};
	buf.pAudioData = soundData.pBuffer;
	buf.AudioBytes = soundData.bufferSize;
	buf.Flags = XAUDIO2_END_OF_STREAM;

	// 波形データの再生
	result = pSourceVoice->SubmitSourceBuffer(&buf);
	assert(SUCCEEDED(result));

	result = pSourceVoice->Start();
	assert(SUCCEEDED(result));
}

void Audio::SoundUnload(SoundData* soundData){
	delete[] soundData->pBuffer;

	soundData->pBuffer = nullptr;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}
