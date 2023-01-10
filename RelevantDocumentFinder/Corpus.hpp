#include <fstream>
#include <string>
#include <numeric>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <initializer_list>
#include <queue>
#include <optional>
#include <functional>
#include <shared_mutex>


namespace RelDocFinder
{
	using ulong = unsigned long;
	using DocId = ulong;

	struct string_view_hash
	{
		using is_transparent = std::true_type;

		auto operator()(std::string_view sv) const noexcept
		{
			return std::hash<std::string_view>()(sv);
		}
	};

	struct string_view_equal
	{
		using is_transparent = std::true_type;

		bool operator()(std::string_view a, std::string_view b) const noexcept
		{
			return a == b;
		}
	};


	class Corpus
	{
	public:
		explicit Corpus() = default;

		explicit Corpus(std::string_view csvFilePath);  // init with csv file, each line is considered as a document

		[[nodiscard]] std::optional<std::string_view> getDocument(const DocId docId) const noexcept;

		[[nodiscard]] bool deleteDocument(const DocId docId) noexcept;

		[[nodiscard]] bool addDocument(const DocId docId, std::string_view doc) noexcept;

		[[nodiscard]] bool updateDocument(const DocId docId, std::string_view doc) noexcept;

		[[nodiscard]] bool addOrUpdateDocument(const DocId docId, std::string_view doc) noexcept;

		[[nodiscard]] std::unique_ptr<std::string_view[]> searchQuery(std::string_view query, const std::size_t n) const noexcept;

	private:
		using Frequency = ulong;

		// word to document-IDs which it appears in
		// the functors are so unordered_map could look up both std::string and std::string_view
		// as std::string_view can be implicitly constructed from std::string
		using WordToDocIds = std::unordered_map<std::string, std::unordered_set<DocId>, string_view_hash, string_view_equal>;

		// word to its frequency in a document
		using DocumentBag = std::unordered_map<std::string_view, Frequency>;

		// document id to its document bag
		using DocIdToDocumentBag = std::unordered_map<DocId, DocumentBag>;

		using DocIdToDocument = std::unordered_map<DocId, std::string>;


		struct DocInfo
		{
			DocId docId;
			double tfIdfScore;

			// sizeof(DocInfo) is small, so take by value
			friend bool operator<(const DocInfo lhs, const DocInfo rhs) 
			{
				return lhs.tfIdfScore > rhs.tfIdfScore;
			}
		};


		WordToDocIds wordToDocIds_;							// stores strings
		DocIdToDocumentBag docIdToDocBag_;					// stores string_views
		std::unordered_map<DocId, ulong> docIdToSize_;
		DocIdToDocument docIdToDocument_;					// stores strings

		mutable std::shared_mutex mutex_;


		static DocumentBag getDocumentBag(std::string_view doc);


		std::priority_queue<DocInfo> searchAndRank(const DocumentBag& queryBag, const std::size_t n) const noexcept;

		std::unique_ptr<std::string_view[]> obtainQueryResult(std::priority_queue<DocInfo>& minHeap, const std::size_t n) const noexcept;
	};
}