#include "Corpus.hpp"

#include <ranges>

namespace RelDocFinder
{
	DocumentBag Corpus::getDocumentBag(std::string_view doc)
	{
		DocumentBag docBag{};

		std::string::size_type start{ 0U };

		while (start < doc.size())
		{
			auto end = doc.find_first_of(' ', start);
			if (start != end)
			{
				std::string_view wrd{ doc.substr(start, end - start) };
				++docBag[wrd];
			}

			if (end == std::string_view::npos)
				break;

			start = end + 1U;
		}

		return docBag;
	}

	bool Corpus::compareByScore(const std::pair<double, DocId>& a, const std::pair<double, DocId>& b)
	{
		return a.first > b.first;
	}

	Corpus::Corpus(std::string_view csvFilePath)
	{
		std::ifstream csvFile{ csvFilePath.data() };

		DocId docId{ 0U };
		std::string doc{ "" };

		for (std::string line; std::getline(csvFile, line); )
		{
			auto delimPos{ line.find(',') };
			std::string_view svDocId{ line.begin(), line.begin() + delimPos };
			docId = std::strtoul(svDocId.data(), nullptr, 10);

			std::string_view svDoc{ line.begin() + delimPos + 1U, line.end() };
			m_docIdToDocument.emplace(docId, svDoc);

			m_docIdToDocBag.emplace(docId, Corpus::getDocumentBag(m_docIdToDocument.at(docId)));

			ulong docSize{ 0U };
			for (const std::pair<std::string_view, Frequency>& entry : m_docIdToDocBag.at(docId))
			{
				docSize += entry.second;
				auto it = m_wordToDocIds.find(entry.first);
				if (it != m_wordToDocIds.end())
				{
					it->second.insert(docId);
				}
				else
				{
					m_wordToDocIds.emplace(entry.first, std::initializer_list<DocId>{ docId });
				}
			}

			m_docIdToSize.emplace(docId, docSize);
		}
	}

	std::optional<std::string_view> Corpus::getDocument(DocId docId) const noexcept
	{
		std::shared_lock lock{ m_mutex };

		if (m_docIdToDocument.contains(docId)) [[likely]]
		{
			return m_docIdToDocument.at(docId);
		}
		else [[unlikely]]
		{
			return { };
		}
	}

	bool Corpus::deleteDocument(DocId docId) noexcept
	{
		std::unique_lock lock{ m_mutex };

		if (!m_docIdToDocument.contains(docId)) [[unlikely]]
		{
			return false;
		}

		for (const std::pair<std::string_view, Frequency>& entry : m_docIdToDocBag.at(docId))
		{
			auto it = m_wordToDocIds.find(entry.first);
			if (it != m_wordToDocIds.end())
			{
				it->second.erase(docId);
				if (it->second.empty())
				{
					m_wordToDocIds.erase(it);
				}
			}
		}

		m_docIdToDocBag.erase(docId);
		m_docIdToSize.erase(docId);
		m_docIdToDocument.erase(docId);

		return true;
	}

	bool Corpus::addDocument(DocId docId, std::string_view doc) noexcept
	{
		std::unique_lock lock{ m_mutex };

		if (doc.empty() || m_docIdToDocument.contains(docId)) [[unlikely]]
		{
			return false;
		}

		m_docIdToDocument.emplace(docId, doc);

		m_docIdToDocBag.emplace(docId, Corpus::getDocumentBag(m_docIdToDocument.at(docId)));

		ulong docSize{ 0U };
		for (const std::pair<std::string_view, Frequency>& entry : m_docIdToDocBag.at(docId))
		{
			docSize += entry.second;
			auto it = m_wordToDocIds.find(entry.first);
			if (it != m_wordToDocIds.end())
			{
				it->second.insert(docId);
			}
			else
			{
				m_wordToDocIds.emplace(entry.first, std::initializer_list<DocId>{ docId });
			}
		}

		m_docIdToSize.emplace(docId, docSize);

		return true;
	}

	bool Corpus::updateDocument(DocId docId, std::string_view doc) noexcept
	{
		bool delSuccess{ deleteDocument(docId) };
		if (delSuccess) [[likely]]
		{
			return addDocument(docId, doc);
		}
		else [[unlikely]]
		{
			return false;
		}
	}

	bool Corpus::addOrUpdateDocument(DocId docId, std::string_view doc) noexcept
	{
		if (m_docIdToDocument.contains(docId))
		{
			return updateDocument(docId, doc);
		}
		else
		{
			return addDocument(docId, doc);
		}
	}

	std::unique_ptr<std::string_view[]> Corpus::searchQuery(std::string_view query, std::size_t n) const noexcept
	{
		std::shared_lock lock{ m_mutex };

		DocumentBag queryBag{ Corpus::getDocumentBag(query) };

		DocScoreMinHeap minHeap{ searchAndRank(queryBag, n) };

		std::unique_ptr<std::string_view[]> queryResult{ obtainQueryResult(minHeap, n) };

		return queryResult;
	}
	
	DocScoreMinHeap Corpus::searchAndRank(const DocumentBag& queryBag, std::size_t n) const noexcept
	{
		DocScoreMinHeap minHeap{ Corpus::compareByScore }; // min heap of tfidf score --> docId

		// tf(term, document) = #(occurences of term in document) / #(words in document)

		// idf(term, corpus) = log(size(corpus) / #(documents which contain the term))

		// tfidf(term, document, corpus) = tf * idf

		for (const auto& docIdToBag : m_docIdToDocBag)
		{
			DocId docId = docIdToBag.first;
			const DocumentBag& currDocBag = docIdToBag.second;
			ulong docSize{ m_docIdToSize.at(docId) };

			double tfidf{ 0.0 };

			for (std::string_view term : std::ranges::views::keys(queryBag))
			{
				double tf{ 0.0 };
				if (auto searchTermInCurrDoc = currDocBag.find(term); searchTermInCurrDoc != currDocBag.end())
				{
					tf += double(searchTermInCurrDoc->second) / docSize;
				}

				std::size_t nDocsWhichContainTerm{ 0U };
				if (auto searchCorpus = m_wordToDocIds.find(term); searchCorpus != m_wordToDocIds.end())
				{
					nDocsWhichContainTerm += std::size(searchCorpus->second);
				}

				double idf{ log(std::size(m_docIdToDocument) / nDocsWhichContainTerm) };

				tfidf += tf * idf;
			}

			minHeap.emplace(tfidf, docId);
			if (minHeap.size() == n + 1U)
			{
				minHeap.pop();
			}
		}

		return minHeap;
	}

	std::unique_ptr<std::string_view[]> Corpus::obtainQueryResult(DocScoreMinHeap& minHeap, std::size_t n) const noexcept
	{
		std::unique_ptr<std::string_view[]> queryResult = std::make_unique<std::string_view[]>(n);
		std::size_t idx{ minHeap.size() - 1U };
		while (!minHeap.empty())
		{
			queryResult[idx] = m_docIdToDocument.at(minHeap.top().second);
			--idx;
			minHeap.pop();
		}
		return queryResult;
	}
}