// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

// WARNING! Do not edit this file manually, your changes will be overwritten.

#pragma once

#ifndef PAGEINFOOBJECT_H
#define PAGEINFOOBJECT_H

#include "TodaySchema.h"

namespace graphql::today::object {
namespace methods::PageInfoHas {

template <class TImpl>
concept getHasNextPageWithParams = requires (TImpl impl, service::FieldParams params) 
{
	{ service::AwaitableScalar<bool> { impl.getHasNextPage(std::move(params)) } };
};

template <class TImpl>
concept getHasNextPage = requires (TImpl impl) 
{
	{ service::AwaitableScalar<bool> { impl.getHasNextPage() } };
};

template <class TImpl>
concept getHasPreviousPageWithParams = requires (TImpl impl, service::FieldParams params) 
{
	{ service::AwaitableScalar<bool> { impl.getHasPreviousPage(std::move(params)) } };
};

template <class TImpl>
concept getHasPreviousPage = requires (TImpl impl) 
{
	{ service::AwaitableScalar<bool> { impl.getHasPreviousPage() } };
};

template <class TImpl>
concept beginSelectionSet = requires (TImpl impl, const service::SelectionSetParams params) 
{
	{ impl.beginSelectionSet(params) };
};

template <class TImpl>
concept endSelectionSet = requires (TImpl impl, const service::SelectionSetParams params) 
{
	{ impl.endSelectionSet(params) };
};

} // namespace methods::PageInfoHas

class PageInfo
	: public service::Object
{
private:
	service::AwaitableResolver resolveHasNextPage(service::ResolverParams&& params) const;
	service::AwaitableResolver resolveHasPreviousPage(service::ResolverParams&& params) const;

	service::AwaitableResolver resolve_typename(service::ResolverParams&& params) const;

	struct Concept
	{
		virtual ~Concept() = default;

		virtual void beginSelectionSet(const service::SelectionSetParams& params) const = 0;
		virtual void endSelectionSet(const service::SelectionSetParams& params) const = 0;

		virtual service::AwaitableScalar<bool> getHasNextPage(service::FieldParams&& params) const = 0;
		virtual service::AwaitableScalar<bool> getHasPreviousPage(service::FieldParams&& params) const = 0;
	};

	template <class T>
	struct Model
		: Concept
	{
		Model(std::shared_ptr<T>&& pimpl) noexcept
			: _pimpl { std::move(pimpl) }
		{
		}

		service::AwaitableScalar<bool> getHasNextPage(service::FieldParams&& params) const final
		{
			if constexpr (methods::PageInfoHas::getHasNextPageWithParams<T>)
			{
				return { _pimpl->getHasNextPage(std::move(params)) };
			}
			else if constexpr (methods::PageInfoHas::getHasNextPage<T>)
			{
				return { _pimpl->getHasNextPage() };
			}
			else
			{
				throw std::runtime_error(R"ex(PageInfo::getHasNextPage is not implemented)ex");
			}
		}

		service::AwaitableScalar<bool> getHasPreviousPage(service::FieldParams&& params) const final
		{
			if constexpr (methods::PageInfoHas::getHasPreviousPageWithParams<T>)
			{
				return { _pimpl->getHasPreviousPage(std::move(params)) };
			}
			else if constexpr (methods::PageInfoHas::getHasPreviousPage<T>)
			{
				return { _pimpl->getHasPreviousPage() };
			}
			else
			{
				throw std::runtime_error(R"ex(PageInfo::getHasPreviousPage is not implemented)ex");
			}
		}

		void beginSelectionSet(const service::SelectionSetParams& params) const final
		{
			if constexpr (methods::PageInfoHas::beginSelectionSet<T>)
			{
				_pimpl->beginSelectionSet(params);
			}
		}

		void endSelectionSet(const service::SelectionSetParams& params) const final
		{
			if constexpr (methods::PageInfoHas::endSelectionSet<T>)
			{
				_pimpl->endSelectionSet(params);
			}
		}

	private:
		const std::shared_ptr<T> _pimpl;
	};

	PageInfo(std::unique_ptr<Concept>&& pimpl) noexcept;

	service::TypeNames getTypeNames() const noexcept;
	service::ResolverMap getResolvers() const noexcept;

	void beginSelectionSet(const service::SelectionSetParams& params) const final;
	void endSelectionSet(const service::SelectionSetParams& params) const final;

	const std::unique_ptr<Concept> _pimpl;

public:
	template <class T>
	PageInfo(std::shared_ptr<T> pimpl) noexcept
		: PageInfo { std::unique_ptr<Concept> { std::make_unique<Model<T>>(std::move(pimpl)) } }
	{
	}
};

} // namespace graphql::today::object

#endif // PAGEINFOOBJECT_H
