// Pedro Vernetti Gonçalves

#pragma once
#ifndef _RAWYCBCRVIDEO_INCLUDED
#define _RAWYCBCRVIDEO_INCLUDED

#include <cstdio>
#include <sys/stat.h>
#include <sys/mman.h>
#include <vector>
#include <array>

#ifdef _OPENMP

#else
#endif

class rawYCbCrVideo
{
    public:

    using sample = uint8_t;

    protected:

    const sample * data;
    size_t totalSamples;
    size_t width;
    size_t height;

    size_t YChannelSize;
    size_t CChannelSize;
    size_t frameSize;

    constexpr size_t nthFramePosition( const size_t n ) const noexcept
    {
        return (n * this->frameSize);
    }

    constexpr size_t nthFrameYPosition( const size_t n ) const noexcept
    {
        return nthFramePosition(n);
    }

    constexpr size_t nthFrameCbPosition( const size_t n ) const noexcept
    {
        return (nthFramePosition(n) + this->YChannelSize);
    }

    constexpr size_t nthFrameCrPosition( const size_t n ) const noexcept
    {
        return (nthFramePosition(n) + this->YChannelSize + this->CChannelSize);
    }

    constexpr static size_t nthBlockPos( const size_t n,
                                         const uint8_t blockSize,
                                         const size_t frameWidth )
    {
        // retorna a posição absoluta dentro do frame, da posição (0,0) do bloco N
        const size_t p = n * blockSize;
        return (p % frameWidth) + ((p / frameWidth) * blockSize * frameWidth);
    }

    static bool blocksAreEqual( const sample * const frameA,
                                const size_t fAblockPos,
                                const sample * const frameB,
                                const size_t fBblockPos,
                                const size_t blockSize,
                                const size_t frameWidth )
    {
        // tentei paralelizar esse for de várias formas. todas foram horríveis
        // aumentando o tempo de cada comparação de quadros por um fator de 1.2 a 1.5
        // (tentei não só parallel for, mas tb outras coisas, como dar cada linha do bloco pra uma thread
        // separada, de várias formas, como usando sections em diferentes profundidades da coisa)
        // (ele na forma atual não ta "pronto pra paralelizar", eu sei, mas como
        // decidi não usar o openmp aqui, deixei ele no formato mais rápido que encontrei)
        for (size_t line = 0, lineA, lineB; line < blockSize; line += frameWidth)
        {
            lineA = fAblockPos + line;
            lineB = fBblockPos + line;
            for (size_t column = 0; column < blockSize; column++)
            {
                if (frameA[lineA + column] != frameB[lineB + column])
                    return false;
            }
        }
        return true;
    }

    public:

    rawYCbCrVideo( const std::string & filepath, const size_t width, const size_t height ) :
        data(nullptr), totalSamples(0),
        width(width), height(height),
        YChannelSize(width * height), CChannelSize((width * height) / 4),
        frameSize((width * height * 15) / 10)
    {
        FILE * f = std::fopen(filepath.data(), "rb");
        if (f == nullptr) return;
        struct stat stat;
        if ((fstat(f->_fileno, &stat) == -1) || (stat.st_size == 0)) return;
        this->totalSamples = stat.st_size;
        this->data = (const sample *)(mmap(0, stat.st_size, PROT_READ, MAP_SHARED, f->_fileno, 0));
    }

    ~rawYCbCrVideo()
    {
        munmap((void *)(this->data), this->totalSamples);
    }

    constexpr size_t samplesPerFrame() const noexcept
    {
        return this->frameSize;
    }

    constexpr size_t samplesPerYChannel() const noexcept
    {
        return this->YChannelSize;
    }

    constexpr size_t samplesPerCChannel() const noexcept
    {
        return this->CChannelSize;
    }

    constexpr size_t totalFrames() const noexcept
    {
        return (this->totalSamples / this->frameSize);
    }

    const sample * nthFrame( const size_t n ) const
    {
        return &(this->data[this->nthFramePosition(n)]);
    }

    std::vector<std::pair<size_t, size_t>> temporalRedundancies( const size_t refFrame,
                                                                 const size_t analyzedFrame,
                                                                 const uint8_t blockSize = 8 )
    {
        const sample * const rf = this->nthFrame(refFrame);
        const sample * const af = this->nthFrame(analyzedFrame);
        const size_t totalBlocks = this->YChannelSize / (blockSize * blockSize);
        const size_t refBlockMaxPos = // ignora as últimas linhas, onde não seria verticalmente possível pegar um bloco inteiro
            this->YChannelSize - (this->width * (blockSize - 1)) - (blockSize - 1);

        // vetor de pares de posições iniciais de bloco, no frame de referência e no frame analizado
        std::vector<std::pair<size_t, size_t>> redundantBlocks;
        redundantBlocks.reserve(totalBlocks);

        // é uma única linhazinha efetiva de OpenMP, que era o foco do trabalho...
        // mas foi, de longe, o que deu o melhor resultado, levando ~350ms por chamada
        // dessa função, no meu note, em comparação a ~2300ms sem openmp / sem essa linha
        // (e as várias coisas que eu fiz com openmp, aqui, que não ficaram visíveis, eu
        // tentei transmitir nos comentários, como dá pra ver)
        #ifdef _OPENMP
        #pragma omp parallel for schedule(static, 160)
        #endif
        for (size_t i = 0; i < totalBlocks; i++)
        {
            // não consegui pensar em formas de tornar "openmp-friendly" esse for em particular
            // pois sem o artifício do break, ali, não vi como não ficar nesse loop muito além do necessário
            for (size_t rb = 0, b = nthBlockPos(++i, 8, this->width); rb < refBlockMaxPos; rb++)
            {
                if ((this->width - (rb % this->width)) < blockSize)
                    continue; // ignora posições onde não é horizontalmente possível pegar um bloco inteiro
                if (blocksAreEqual(rf, rb, af, b, 8, this->width))
                {
                    redundantBlocks[i] = {rb, b};
                    break;
                }
            }
        }
        return redundantBlocks;

        // as várias coisas que eu testei aqui, inicialmente sofrendo levando 6 segundos
        // pra cada execução dessa função.. até chegar a 2~ segundos, me fizeram sinceramente
        // entender que mínimos detalhes dentro de um for fazem uma diferença absurda,
        // além de simplesmente tornarem ele paralelizável ou não
    }

    void saveTo( const std::string & filepath ) const
    {
        FILE * f = std::fopen(filepath.data(), "wb");
        std::fwrite(this->data, 1, this->totalSamples, f);
        std::fclose(f);
    }

    constexpr operator bool () const noexcept
    {
        return ((this->data != nullptr) && this->totalSamples);
    }
};

using YUVVideo = rawYCbCrVideo;

/* template <uint8_t blockSize = 8>
class compressedYCbCrVideo
{
    public:

    using sample = uint8_t;
    using block = std::array<sample, blockSize>;

    protected:

    block * blocks;
    const std::vector<sample *> yFrames;
    const std::vector<sample *> yFrames;
    size_t width;
    size_t height;

    public:

    compressedYCbCrVideo() = delete;

    compressedYCbCrVideo( const rawYCbCrVideo & v ) :
        blocks(),
    {

        auto frame10 = v.nthFrame(10);
        for (size_t i = 0; i < v.samplesPerYChannel(); i++) std::cout << frame10[i] << " ";
    }

    void saveTo( const std::string & filepath )
    {
        FILE * f = std::fopen(filepath.data(), "wb");
        std::fwrite(this->data, 1, this->totalSamples, f);
        std::fclose(f);
    }
}; */

/*
Estrutura do .yuv:

quadro
    canal (Y = WxH. Cb = (WxH/4). Cr = (WxH/4))
        amostras em sentido de leitura (-> V) (=byte)
        amostra
        amostra
        amostra
    canal
    canal
    canal
quadro
quadro
quadro
*/

#endif // _RAWYCBCRVIDEO_INCLUDED
