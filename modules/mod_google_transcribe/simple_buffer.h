class SimpleBuffer {
  public:
    SimpleBuffer(uint32_t chunkSize, uint32_t numChunks) : numItems(0),
    m_numChunks(numChunks), m_chunkSize(chunkSize) {
      pData = new char[chunkSize * numChunks];
      pNextWrite = pData;
    }
    ~SimpleBuffer() {
      delete [] pData;
    }

    void add(void *data, uint32_t datalen) {
      memcpy(pNextWrite, data, datalen);
      if (numItems < m_numChunks) numItems++;

      uint32_t offset = (pNextWrite - pData) / m_chunkSize;
      if (offset >= m_numChunks - 1) pNextWrite = pData;
      else pNextWrite += m_chunkSize;
    }

    char* getNextChunk() {
      if (numItems--) {
        char *p = pNextWrite;
        uint32_t offset = (pNextWrite - pData) / m_chunkSize;
        if (offset >= m_numChunks - 1) pNextWrite = pData;
        else pNextWrite += m_chunkSize;
        return p;
      }
      return nullptr;
    }

    uint32_t getNumItems() { return numItems;}

  private:
    char *pData;
    uint32_t numItems;
    uint32_t m_chunkSize;
    uint32_t m_numChunks;
    char* pNextWrite;
};
