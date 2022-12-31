#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "Corpus.hpp"


TEST_CASE("Corpus", "[Corpus]")
{
	RelDocFinder::Corpus corpus{ "init_docs.txt" };
	
	SECTION("Corpus::getDocument")
	{
		std::optional<std::string_view> badDoc1 = corpus.getDocument(-1);
		REQUIRE(!badDoc1.has_value());

		std::optional<std::string_view> goodDoc0 = corpus.getDocument(0U);
		REQUIRE(goodDoc0 == "happy day");

		std::optional<std::string_view> goodDoc1 = corpus.getDocument(1U);
		REQUIRE(*goodDoc1 == "happy");

		std::optional<std::string_view> goodDoc2 = corpus.getDocument(2U);
		REQUIRE(*goodDoc2 == "day");

		std::optional<std::string_view> goodDoc3 = corpus.getDocument(3U);
		REQUIRE(*goodDoc3 == "have a nice day");

		std::optional<std::string_view> goodDoc4 = corpus.getDocument(4U);
		REQUIRE(*goodDoc4 == "colorless green ideas sleep furiously");

		std::optional<std::string_view> badDoc17 = corpus.getDocument(17U);
		REQUIRE(!badDoc17.has_value());
	}

	SECTION("Corpus::searchQuery")
	{
		constexpr int n{ 3 };
		std::unique_ptr<std::string_view[]> queryRes = corpus.searchQuery("happy day", n);

		constexpr std::string_view expected[] = { "happy", "happy day", "colorless green ideas sleep furiously" };

		for (int i = 0; i < n; ++i)
		{
			REQUIRE(queryRes[i] == expected[i]);
		}
	}

	SECTION("Corpus::addDocument")
	{
		REQUIRE(!corpus.addDocument(5U, ""));
		REQUIRE(!corpus.addDocument(0U, "happy day"));

		REQUIRE(corpus.addDocument(5U, "green dog"));
		std::optional<std::string_view> doc5 = corpus.getDocument(5U);
		REQUIRE(*doc5 == "green dog");

		constexpr int n{ 3 };
		std::unique_ptr<std::string_view[]> queryRes = corpus.searchQuery("green", n);

		constexpr std::string_view expected[] = { "green dog", "colorless green ideas sleep furiously", "happy" };

		for (int i = 0; i < n; ++i)
		{
			REQUIRE(queryRes[i] == expected[i]);
		}
	}

	SECTION("Corpus::deleteDocument")
	{
		REQUIRE(!corpus.deleteDocument(-1));
		REQUIRE(!corpus.deleteDocument(57U));

		REQUIRE(corpus.deleteDocument(0U));

		constexpr int n{ 3 };
		std::unique_ptr<std::string_view[]> queryRes = corpus.searchQuery("happy day", n);

		constexpr std::string_view expected[] = { "happy", "day",  "have a nice day" };

		for (int i = 0; i < n; ++i)
		{
			REQUIRE(queryRes[i] == expected[i]);
		}
	}

	SECTION("Corpus::updateDocument")
	{
		REQUIRE(corpus.updateDocument(3U, "happy day"));

		constexpr int n{ 3 };
		std::unique_ptr<std::string_view[]> queryRes = corpus.searchQuery("happy day", n);

		constexpr std::string_view expected[] = { "happy day", "happy", "colorless green ideas sleep furiously" };

		for (int i = 0; i < n; ++i)
		{
			REQUIRE(queryRes[i] == expected[i]);
		}
	}
}