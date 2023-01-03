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
			docIdToDocument_.emplace(docId, svDoc);

			docIdToDocBag_.emplace(docId, Corpus::getDocumentBag(docIdToDocument_.at(docId)));

			ulong docSize{ 0U };
			for (const std::pair<std::string_view, Frequency>& entry : docIdToDocBag_.at(docId))
			{
				docSize += entry.second;
				auto it = wordToDocIds_.find(entry.first);
				if (it != wordToDocIds_.end())
				{
					it->second.insert(docId);
				}
				else
				{
					wordToDocIds_.emplace(entry.first, std::initializer_list<DocId>{ docId });
				}
			}

			docIdToSize_.emplace(docId, docSize);
		}
	}

	std::optional<std::string_view> Corpus::getDocument(DocId docId) const noexcept
	{
		std::shared_lock lock{ mutex_ };

		if (docIdToDocument_.contains(docId)) [[likely]]
		{
			return docIdToDocument_.at(docId);
		}
		else [[unlikely]]
		{
			return { };
		}
	}

	bool Corpus::deleteDocument(DocId docId) noexcept
	{
		std::unique_lock lock{ mutex_ };

		if (!docIdToDocument_.contains(docId)) [[unlikely]]
		{
			return false;
		}

		for (const std::pair<std::string_view, Frequency>& entry : docIdToDocBag_.at(docId))
		{
			auto it = wordToDocIds_.find(entry.first);
			if (it != wordToDocIds_.end())
			{
				it->second.erase(docId);
				if (it->second.empty())
				{
					wordToDocIds_.erase(it);
				}
			}
		}

		docIdToDocBag_.erase(docId);
		docIdToSize_.erase(docId);
		docIdToDocument_.erase(docId);

		return true;
	}

	bool Corpus::addDocument(DocId docId, std::string_view doc) noexcept
	{
		std::unique_lock lock{ mutex_ };

		if (doc.empty() || docIdToDocument_.contains(docId)) [[unlikely]]
		{
			return false;
		}

		docIdToDocument_.emplace(docId, doc);

		docIdToDocBag_.emplace(docId, Corpus::getDocumentBag(docIdToDocument_.at(docId)));

		ulong docSize{ 0U };
		for (const std::pair<std::string_view, Frequency>& entry : docIdToDocBag_.at(docId))
		{
			docSize += entry.second;
			auto it = wordToDocIds_.find(entry.first);
			if (it != wordToDocIds_.end())
			{
				it->second.insert(docId);
			}
			else
			{
				wordToDocIds_.emplace(entry.first, std::initializer_list<DocId>{ docId });
			}
		}

		docIdToSize_.emplace(docId, docSize);

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
		if (docIdToDocument_.contains(docId))
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
		std::shared_lock lock{ mutex_ };

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

		for (const auto& docIdToBag : docIdToDocBag_)
		{
			DocId docId = docIdToBag.first;
			const DocumentBag& currDocBag = docIdToBag.second;
			ulong docSize{ docIdToSize_.at(docId) };

			double tfidf{ 0.0 };

			for (std::string_view term : std::ranges::views::keys(queryBag))
			{
				double tf{ 0.0 };
				if (auto searchTermInCurrDoc = currDocBag.find(term); searchTermInCurrDoc != currDocBag.end())
				{
					tf += double(searchTermInCurrDoc->second) / docSize;
				}

				std::size_t nDocsWhichContainTerm{ 1U };
				if (auto searchCorpus = wordToDocIds_.find(term); searchCorpus != wordToDocIds_.end())
				{
					nDocsWhichContainTerm = std::size(searchCorpus->second);
				}

				double idf{ log(std::size(docIdToDocument_) / nDocsWhichContainTerm) };

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
			queryResult[idx] = docIdToDocument_.at(minHeap.top().second);
			--idx;
			minHeap.pop();
		}
		return queryResult;
	}
}