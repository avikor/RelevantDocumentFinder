#include "Corpus.hpp"

#include <ranges>
#include <mutex>
#include <cmath>


namespace RelDocFinder
{
	Corpus::DocumentBag Corpus::getDocumentBag(std::string_view doc)
	{
		DocumentBag docBag{};

		std::string::size_type start{ 0U };

		while (start < doc.size())
		{
			const auto end = doc.find_first_of(' ', start);
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

	Corpus::Corpus(std::string_view csvFilePath)
	{
		std::ifstream csvFile{ csvFilePath.data() };

		DocId docId{ 0U };

		for (std::string line{}; std::getline(csvFile, line); )
		{
			const auto delimPos{ line.find(',') };
			std::string_view svDocId{ line.begin(), line.begin() + delimPos };
			docId = std::strtoul(svDocId.data(), nullptr, 10);

			std::string_view svDoc{ line.begin() + delimPos + 1U, line.end() };
			docIdToDocument_.emplace(docId, svDoc);

			docIdToDocBag_.emplace(docId, Corpus::getDocumentBag(docIdToDocument_.at(docId)));

			ulong docSize{ 0U };
			for (const std::pair<std::string_view, Frequency>& entry : docIdToDocBag_.at(docId))
			{
				docSize += entry.second;
				const auto it = wordToDocIds_.find(entry.first);
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

	std::optional<std::string_view> Corpus::getDocument(const DocId docId) const noexcept
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

	bool Corpus::deleteDocument(const DocId docId) noexcept
	{
		std::unique_lock lock{ mutex_ };

		if (!docIdToDocument_.contains(docId)) [[unlikely]]
		{
			return false;
		}

		for (const std::pair<std::string_view, Frequency>& entry : docIdToDocBag_.at(docId))
		{
			const auto it = wordToDocIds_.find(entry.first);
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

	bool Corpus::addDocument(const DocId docId, std::string_view doc) noexcept
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
			const auto it = wordToDocIds_.find(entry.first);
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

	bool Corpus::updateDocument(const DocId docId, std::string_view doc) noexcept
	{
		if (deleteDocument(docId)) [[likely]]
		{
			return addDocument(docId, doc);
		}
		else [[unlikely]]
		{
			return false;
		}
	}

	bool Corpus::addOrUpdateDocument(const DocId docId, std::string_view doc) noexcept
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

	std::unique_ptr<std::string_view[]> Corpus::searchQuery(std::string_view query, const std::size_t n) const noexcept
	{
		std::shared_lock lock{ mutex_ };

		const DocumentBag queryBag{ Corpus::getDocumentBag(query) };

		std::priority_queue<DocInfo> minHeap{ searchAndRank(queryBag, n) };

		std::unique_ptr<std::string_view[]> queryResult{ obtainQueryResult(minHeap, n) };

		return queryResult;
	}
	
	std::priority_queue<Corpus::DocInfo> Corpus::searchAndRank(const DocumentBag& queryBag, const std::size_t n) const noexcept
	{
		std::priority_queue<DocInfo> minHeap{};

		// tf(term, document) = #(occurences of term in document) / #(words in document)

		// idf(term, corpus) = log(size(corpus) / #(documents which contain the term))

		// tfidf(term, document, corpus) = tf * idf

		double corpusSize{ static_cast<double>(std::size(docIdToDocument_)) };

		for (const auto& docIdToBag : docIdToDocBag_)
		{
			const DocId docId = docIdToBag.first;
			const DocumentBag& currDocBag = docIdToBag.second;
			const double docSize{ static_cast<double>(docIdToSize_.at(docId)) };

			double tfidf{ 0.0 };

			for (std::string_view term : std::ranges::views::keys(queryBag))
			{
				double tf{ 0.0 };
				if (auto searchTermInCurrDoc = currDocBag.find(term); searchTermInCurrDoc != currDocBag.end())
				{
					tf += searchTermInCurrDoc->second / docSize;
				}

				double nDocsWhichContainTerm{ 1 };
				if (auto searchCorpus = wordToDocIds_.find(term); searchCorpus != wordToDocIds_.end())
				{
					nDocsWhichContainTerm = static_cast<double>(std::size(searchCorpus->second));
				}

				const double idf{ std::log10(corpusSize / nDocsWhichContainTerm) };

				tfidf += tf * idf;
			}

			minHeap.emplace(docId, tfidf);
			if (minHeap.size() == n + 1U)
			{
				minHeap.pop();
			}
		}

		return minHeap;
	}

	std::unique_ptr<std::string_view[]> Corpus::obtainQueryResult(std::priority_queue<DocInfo>& minHeap, const std::size_t n) const noexcept
	{
		std::unique_ptr<std::string_view[]> queryResult = std::make_unique<std::string_view[]>(n);
		std::size_t idx{ minHeap.size() - 1U };
		while (!minHeap.empty())
		{
			queryResult[idx] = docIdToDocument_.at(minHeap.top().docId);
			--idx;
			minHeap.pop();
		}
		return queryResult;
	}
}