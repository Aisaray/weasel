export module WeaselSchemas;
import <boost/serialization/vector.hpp>;
import <boost/serialization/string.hpp>;

export
{
	namespace weasel
	{
		struct Schema
		{
			std::string name;
			std::string id;
		};

		struct Schemas
		{		
			std::vector<Schema> vec;
		};
	}

	namespace boost
	{
		namespace serialization
		{
			template <typename Archive>
			void serialize(Archive& ar, weasel::Schema& s, const unsigned version)
			{
				ar& s.name;
				ar& s.id;
			}

			template <typename Archive>
			void serialize(Archive& ar, weasel::Schemas& s, const unsigned version)
			{
				ar& s.vec;
			}
		}
	}
}