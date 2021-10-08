#pragma once

#include <bit>     // For std::bit_cast
#include <cstdint> // For sized types
#include <string>  // For std::memcpy

#ifdef BAKERY_PROVIDE_VECTOR
#include <vector>
#endif

#ifdef BAKERY_PROVIDE_STD_ARRAY
#include <array>
#endif


namespace inliner {

   struct header {
      uint8_t  type = 0;       // 0: Generic binary
                               // 1: Image
                               // 2: Dual-image
      uint8_t  bpp = 0;        // Number of channels [1-4]
      uint8_t  padding0[2]{ 0, 0 }; // Padding, maybe embed version. TODO
      uint32_t bit_count = 0;  // Number of bits stored
      

      uint16_t width = 0;      // Width in pixels
      uint16_t height = 0;     // Height in pixels
      uint16_t padding1[2]{0, 0};

      uint32_t color0 = 0;     // Replacement colors
      uint32_t color1 = 0;     // Replacement colors
   };
   static_assert(sizeof(header) == 24);

   template<int array_size>
   [[nodiscard]] constexpr auto get_header(const uint64_t(&source)[array_size]) -> header;

   template<int array_size>
   [[nodiscard]] constexpr auto is_image  (const uint64_t(&source)[array_size]) -> bool;

   template<int array_size>
   [[nodiscard]] constexpr auto get_width (const uint64_t(&source)[array_size]) -> int;

   template<int array_size>
   [[nodiscard]] constexpr auto get_height(const uint64_t(&source)[array_size]) -> int;

   // These methods provide easy access to the number of elements in the dataset. In images, that equates to the number
   // of pixels. In generic binaries, that's the number of elements of the target type.
   // Can be called with a decoded header (faster), or the dataset itself. Can be called without a target type if the
   // dataset is an image, otherwise the target type has to be provided as template parameter. Throws if that
   // assumption is violated.
   // Every function is constexpr.
   [[nodiscard]] constexpr auto get_element_count(const header& head) -> int;

   template<typename user_type>
   [[nodiscard]] constexpr auto get_element_count(const header& head) -> int;

   template<int array_size>
   [[nodiscard]] constexpr auto get_element_count(const uint64_t(&source)[array_size]) -> int;

   template<typename user_type, int array_size>
   [[nodiscard]] constexpr auto get_element_count(const uint64_t(&source)[array_size]) -> int;


#ifdef BAKERY_PROVIDE_STD_ARRAY
   template<typename user_type, header head, int array_size, int element_count = get_element_count<user_type>(head)>
   [[nodiscard]] constexpr auto decode_to_array(
      const uint64_t(&source)[array_size]
   ) -> std::array<user_type, element_count>;
#endif

#ifdef BAKERY_PROVIDE_VECTOR
   template<typename user_type, int source_size>
   [[nodiscard]] auto decode_to_vector(const uint64_t(&source)[source_size]) -> std::vector<user_type>;
#endif


   template<typename user_type, int source_size>
   auto decode_into_pointer(
      const uint64_t(&source)[source_size],
      user_type* dst
   ) -> void;

} // namespace inliner


namespace inliner::detail {
   template<typename T, int size>
   struct better_array {
      T m_values[size];
      [[nodiscard]] constexpr auto operator[](const int index) const -> const T&;
   };

   template<typename user_type, int element_count>
   struct wrapper_type {
      uint64_t filler[3];
      alignas(user_type) std::array<user_type, element_count> m_data; // needs to be returned
      // End padding should be unnecessary because of array alignment
   };

   template<int bpp>
   struct color_type {
      uint8_t m_components[bpp];
   };
   template<int bpp>
   struct color_pair_type {
      color_type<bpp> color0;
      color_type<bpp> color1;
   };


   template<int bpp>
   [[nodiscard]] constexpr auto get_sized_color_pair(const header& head) -> color_pair_type<bpp>
   {
      const auto head_color0 = std::bit_cast<color_type<4>>(head.color0);
      const auto head_color1 = std::bit_cast<color_type<4>>(head.color1);

      color_type<bpp> color0{};
      color_type<bpp> color1{};
      for (int i = 0; i < bpp; ++i)
      {
         color0.m_components[i] = head_color0.m_components[i];
         color1.m_components[i] = head_color1.m_components[i];
      }
      return { color0, color1 };
   }


   template<typename T, typename color_type, typename target_type>
   constexpr auto reconstruct(
      const int element_count,
      const uint8_t* source,
      target_type& target,
      const color_type& color0,
      const color_type& color1
   ) -> void
   {
      for (int i = 0; i < element_count; ++i)
      {
         const int byte_index = i / 8;
         const int bit_index = i % 8;
         const int left_shift_amount = 7 - bit_index; // 7 is leftest bit
         const uint8_t mask = 1ui8 << left_shift_amount;
         const bool binary_value = static_cast<bool>((source[byte_index] & mask) >> left_shift_amount);
         const auto replacement_color = std::bit_cast<T>(binary_value ? color1 : color0);
         target[i] = replacement_color;
      }
   }


   [[nodiscard]] constexpr auto get_byte_count_from_bit_count(
      const int bit_count
   ) -> int
   {
      int byte_count = bit_count / 8;
      if ((bit_count % 8) > 0)
         ++byte_count;
      return byte_count;
   }
   
} // namespace inliner::detail


template <typename T, int size>
constexpr auto inliner::detail::better_array<T, size>::operator[](
   const int index
) const -> const T&
{
   return m_values[index];
}


template<int array_size>
constexpr auto inliner::get_header(
   const uint64_t(&source)[array_size]
) -> header
{
   header result;

   {
      struct front_decoder {
         uint8_t  type;
         uint8_t  bpp;
         uint8_t  padding0[2];
         uint32_t bit_count;
      };

      const front_decoder temp = std::bit_cast<front_decoder>(source[0]);
      result.type = temp.type;
      result.bpp = temp.bpp;
      result.bit_count = temp.bit_count;
   }

   if (result.type > 0)
   {
      const auto temp = std::bit_cast<detail::better_array<uint16_t, 4>>(source[1]);
      result.width = temp[0];
      result.height = temp[1];
   }
   if (result.type == 2)
   {
      const auto temp = std::bit_cast<detail::better_array<uint32_t, 2>>(source[2]);
      result.color0 = temp[0];
      result.color1 = temp[1];
   }

   return result;
}


template<int array_size>
constexpr auto inliner::is_image(
   const uint64_t(&source)[array_size]
) -> bool
{
   const auto temp0 = std::bit_cast<detail::better_array<uint8_t, 8>>(source[0]);
   const uint8_t type = temp0[0];
   return type == 1 || type == 2;
}


template<int array_size>
constexpr auto inliner::get_width(
   const uint64_t(&source)[array_size]
) -> int
{
   if (is_image(source) == false)
   {
      return -1;
   }
   const auto temp = std::bit_cast<detail::better_array<uint16_t, 4>>(source[1]);
   return temp[0];
}


template<int array_size>
constexpr auto inliner::get_height(
   const uint64_t(&source)[array_size]
) -> int
{
   if (is_image(source) == false)
   {
      return -1;
   }
   const auto temp = std::bit_cast<detail::better_array<uint16_t, 4>>(source[1]);
   return temp[1];
}


#ifdef BAKERY_PROVIDE_STD_ARRAY
template<typename user_type, inliner::header head, int array_size, int element_count>
constexpr auto inliner::decode_to_array(
   const uint64_t(&source)[array_size]
) -> std::array<user_type, element_count>
{
   static_assert(element_count > 0, "Not enough bytes to even fill one of those types.");

   if constexpr (head.type == 2)
   {
      static_assert(sizeof(user_type) == head.bpp, "User type has different size than bpp");
      const auto [color0, color1] = detail::get_sized_color_pair<head.bpp>(head);

      // First cast into its encoded representation
      const int byte_count = detail::get_byte_count_from_bit_count(head.bit_count);
      using interm_type = detail::wrapper_type<uint8_t, byte_count>;
      const std::array<uint8_t, byte_count> interm = std::bit_cast<interm_type>(source).m_data;

      // Then reconstruct the original colors
      using target_type = std::array<user_type, element_count>;
      target_type result;
      detail::reconstruct<user_type>(element_count, &interm[0], result, color0, color1);
      return result;
   }
   else
   {
      using target_type = detail::wrapper_type<user_type, element_count>;
      return std::bit_cast<target_type>(source).m_data;
   }
}
#endif


#ifdef BAKERY_PROVIDE_VECTOR
template<typename user_type, int source_size>
auto inliner::decode_to_vector(
   const uint64_t(&source)[source_size]
) -> std::vector<user_type>
{
   const header head = inliner::get_header(source);
   const int element_count = get_element_count<user_type>(head);
   const int byte_count = element_count * sizeof(user_type);

   std::vector<user_type> result(element_count);
   const uint8_t* data_start_ptr = reinterpret_cast<const uint8_t*>(&source[3]);
   if (head.type == 2)
   {
      constexpr int bpp = sizeof(user_type);
      const auto [color0, color1] = detail::get_sized_color_pair<bpp>(head);
      detail::reconstruct<user_type>(element_count, data_start_ptr, result, color0, color1);
   }
   else
   {
      std::memcpy(result.data(), data_start_ptr, byte_count);
   }
   return result;
}
#endif


template<typename user_type, int source_size>
auto inliner::decode_into_pointer(
   const uint64_t(&source)[source_size],
   user_type* dst
) -> void
{
   const header head = inliner::get_header(source);
   const int element_count = get_element_count<user_type>(head);
   const int byte_count = element_count * sizeof(user_type);
   const uint8_t* data_start_ptr = reinterpret_cast<const uint8_t*>(&source[3]);

   if (head.type == 2)
   {
      constexpr int bpp = sizeof(user_type);
      const auto [color0, color1] = detail::get_sized_color_pair<bpp>(head);
      detail::reconstruct<user_type>(element_count, data_start_ptr, dst, color0, color1);
   }
   else
   {
      std::memcpy(dst, data_start_ptr, byte_count);
   }
}


constexpr auto inliner::get_element_count(
   const header& head
) -> int
{
   if (head.type == 0)
   {
      throw "Can't be used on non-image payloads without a type.";
   }
   else
   {
      const int pixel_count = head.width * head.height;
      return pixel_count;
   }
}


template<typename user_type>
constexpr auto inliner::get_element_count(
   const header& head
) -> int
{
   if (head.type == 2)
   {
      return get_element_count(head);
   }
   else
   {
      // Byte count is exact for everything but Indexed images
      const int byte_count = head.bit_count / 8;
      return byte_count / sizeof(user_type);
   }
}


template<typename user_type, int array_size>
constexpr auto inliner::get_element_count(
   const uint64_t(&source)[array_size]
) -> int
{
   const header head = inliner::get_header(source);
   return get_element_count<user_type>(head);
}


template<int array_size>
constexpr auto inliner::get_element_count(
   const uint64_t(&source)[array_size]
) -> int
{
   const header head = inliner::get_header(source);
   return get_element_count(head);
}