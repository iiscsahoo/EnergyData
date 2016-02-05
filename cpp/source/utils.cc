#include <cap/utils.templates.h>
#ifdef WITH_DEAL_II
#include <deal.II/base/types.h>
#endif

namespace cap
{

template std::vector<int> to_vector(std::string const &s);
template std::vector<float> to_vector(std::string const &s);
template std::vector<double> to_vector(std::string const &s);
template std::vector<std::string> to_vector(std::string const &s);
template std::vector<bool> to_vector(std::string const &s);
#ifdef WITH_DEAL_II
template <>
std::vector<dealii::types::material_id> to_vector(std::string const &s)
{
  std::vector<dealii::types::material_id> v;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ','))
  {
    v.push_back(dealii::types::material_id(
                    boost::lexical_cast<dealii::types::material_id>(item)) -
                dealii::types::material_id('0'));
  }
  return v;
}
#endif

template std::string to_string(std::vector<int> const &v);
template std::string to_string(std::vector<float> const &v);
template std::string to_string(std::vector<double> const &v);
template std::string to_string(std::vector<std::string> const &v);
template std::string to_string(std::vector<bool> const &v);

} // end namespace cap