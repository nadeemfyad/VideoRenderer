/*
 * (C) 2019 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "stdafx.h"
#include <DirectXMath.h>
#include "Helper.h"
#include "resource.h"
#include "FontBitmap.h"
#include "D3D11Font.h"

#define MAX_NUM_VERTICES 400*6

struct VertexFont {
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT2 Tex;
};

inline auto Char2Index(WCHAR ch)
{
	if (ch >= 0x0020 && ch <= 0x007F) {
		return ch - 32;
	}
	if (ch >= 0x00A0 && ch <= 0x00BF) {
		return ch - 64;
	}
	return 0x007F - 32;
}

CD3D11Font::CD3D11Font(const WCHAR* strFontName, DWORD dwHeight, DWORD dwFlags)
	: m_dwFontHeight(dwHeight)
	, m_dwFontFlags(dwFlags)
{
	wcsncpy_s(m_strFontName, strFontName, std::size(m_strFontName));
	m_strFontName[std::size(m_strFontName) - 1] = '\0';

	UINT idx = 0;
	for (WCHAR ch = 0x0020; ch < 0x007F; ch++) {
		m_Characters[idx++] = ch;
	}
	m_Characters[idx++] = 0x25CA; // U+25CA Lozenge
	for (WCHAR ch = 0x00A0; ch <= 0x00BF; ch++) {
		m_Characters[idx++] = ch;
	}
	ASSERT(idx == std::size(m_Characters));
}

CD3D11Font::~CD3D11Font()
{
	InvalidateDeviceObjects();
}

HRESULT CD3D11Font::InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
{
	InvalidateDeviceObjects();
	if (!pDevice || !pDeviceContext) {
		return E_POINTER;
	}

	// Keep a local copy of the device
	m_pDevice = pDevice;
	m_pDevice->AddRef();
	m_pDeviceContext = pDeviceContext;
	m_pDeviceContext->AddRef();

	HRESULT hr = S_OK;
	CFontBitmap fontBitmap;

	hr = fontBitmap.Initialize(m_strFontName, m_dwFontHeight, 0, m_Characters, std::size(m_Characters));
	if (FAILED(hr)) {
		return hr;
	}

	m_uTexWidth  = fontBitmap.GetWidth();
	m_uTexHeight = fontBitmap.GetHeight();
	EXECUTE_ASSERT(S_OK == fontBitmap.GetFloatCoords((FloatRect*)&m_fTexCoords, std::size(m_Characters)));

	LPVOID data;
	DWORD size;
	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_VSH11_FONT));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVertexShader));

	static const D3D11_INPUT_ELEMENT_DESC vertexLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateInputLayout(vertexLayout, std::size(vertexLayout), data, size, &m_pInputLayout));

	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_PSH11_FONT));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreatePixelShader(data, size, nullptr, &m_pPixelShader));

	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = m_uTexWidth;
	texDesc.Height = m_uTexHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	BYTE* pData = nullptr;
	UINT uPitch = 0;
	hr = fontBitmap.Lock(&pData, uPitch);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_SUBRESOURCE_DATA subresData = { pData, uPitch, 0 };
	hr = pDevice->CreateTexture2D(&texDesc, &subresData, &m_pTexture);
	fontBitmap.Unlock();
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateShaderResourceView(m_pTexture, &srvDesc, &m_pShaderResource));

	// create the vertex array
	std::vector<VertexFont> vertices;
	vertices.resize(MAX_NUM_VERTICES);

	D3D11_BUFFER_DESC vertexBufferDesc;
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof(VertexFont) * MAX_NUM_VERTICES;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;
	D3D11_SUBRESOURCE_DATA vertexData = { vertices.data(), 0, 0 };

	hr = m_pDevice->CreateBuffer(&vertexBufferDesc, &vertexData, &m_pVertexBuffer);
	if (FAILED(hr)) {
		return hr;
	}

	// create the index array
	std::vector<uint32_t> indices;
	indices.reserve(MAX_NUM_VERTICES);
	for (uint32_t i = 0; i < indices.size(); i++) {
		indices.emplace_back(i);
	}

	D3D11_BUFFER_DESC indexBufferDesc;
	indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	indexBufferDesc.ByteWidth = sizeof(uint32_t) * MAX_NUM_VERTICES;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA indexData = { indices.data(), 0, 0 };

	hr = m_pDevice->CreateBuffer(&indexBufferDesc, &indexData, &m_pIndexBuffer);

	return hr;
}

void CD3D11Font::InvalidateDeviceObjects()
{

	SAFE_RELEASE(m_pTexture);
	SAFE_RELEASE(m_pShaderResource);

	SAFE_RELEASE(m_pVertexShader);
	SAFE_RELEASE(m_pPixelShader);
	SAFE_RELEASE(m_pInputLayout);
	SAFE_RELEASE(m_pVertexBuffer);
	SAFE_RELEASE(m_pIndexBuffer);

	SAFE_RELEASE(m_pDeviceContext);
	SAFE_RELEASE(m_pDevice);
}

HRESULT CD3D11Font::GetTextExtent(const WCHAR* strText, SIZE* pSize)
{
	if (nullptr == strText || nullptr == pSize) {
		return E_FAIL;
	}

	FLOAT fRowWidth = 0.0f;
	FLOAT fRowHeight = (m_fTexCoords[0].bottom - m_fTexCoords[0].top)*m_uTexHeight;
	FLOAT fWidth = 0.0f;
	FLOAT fHeight = fRowHeight;

	while (*strText) {
		WCHAR c = *strText++;

		if (c == '\n') {
			fRowWidth = 0.0f;
			fHeight += fRowHeight;
			continue;
		}

		auto idx = Char2Index(c);

		FLOAT tx1 = m_fTexCoords[idx].left;
		FLOAT tx2 = m_fTexCoords[idx].right;

		fRowWidth += (tx2 - tx1)*m_uTexWidth;

		if (fRowWidth > fWidth) {
			fWidth = fRowWidth;
		}
	}

	pSize->cx = (LONG)fWidth;
	pSize->cy = (LONG)fHeight;

	return S_OK;
}

HRESULT CD3D11Font::Draw2DText(ID3D11RenderTargetView* pRenderTargetView, const SIZE& rtSize, FLOAT sx, FLOAT sy, D3DCOLOR color, const WCHAR* strText)
{
	if (!m_pDevice || !m_pDeviceContext) {
		return E_ABORT;
	}
	ASSERT(pRenderTargetView);

	HRESULT hr = S_OK;

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	hr = m_pDeviceContext->Map(m_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(hr)) {
		return hr;
	}

	// Adjust for character spacing
	const FLOAT fStartX = (float)(sx * 2) / rtSize.cx - 1;
	float drawX = fStartX;
	float drawY = (float)(-sy * 2) / rtSize.cy + 1;

	// Fill vertex buffer
	VertexFont* pVertices = (VertexFont*)mappedResource.pData;
	UINT nVertices = 0;



	while (*strText) {
		WCHAR c = *strText++;

		if (c == '\n') {
			drawX = fStartX;
			drawY -= (m_fTexCoords[0].bottom - m_fTexCoords[0].top) * m_uTexHeight * 2 / rtSize.cy;
			continue;
		}

		auto idx = Char2Index(c);

		float Width  = (m_fTexCoords[idx].right - m_fTexCoords[idx].left) * m_uTexWidth * 2 / rtSize.cx;
		float Height = (m_fTexCoords[idx].bottom - m_fTexCoords[idx].top) * m_uTexHeight * 2 / rtSize.cy;

		float left   = drawX;
		float right  = drawX + Width;
		float top    = drawY;
		float bottom = drawY - Height;

		const float ltex = m_fTexCoords[idx].left;
		const float ttex = m_fTexCoords[idx].top;
		const float rtex = m_fTexCoords[idx].right;
		const float btex = m_fTexCoords[idx].bottom;

		if (c != 0x0020 && c != 0x00A0) { // Space and No-Break Space
			*pVertices++ = {DirectX::XMFLOAT3(left,  top,    0.0f), DirectX::XMFLOAT2(ltex, ttex)};
			*pVertices++ = {DirectX::XMFLOAT3(right, bottom, 0.0f), DirectX::XMFLOAT2(rtex, btex)};
			*pVertices++ = {DirectX::XMFLOAT3(left,  bottom, 0.0f), DirectX::XMFLOAT2(ltex, btex)};
			*pVertices++ = {DirectX::XMFLOAT3(left,  top,    0.0f), DirectX::XMFLOAT2(ltex, ttex)};
			*pVertices++ = {DirectX::XMFLOAT3(right, top,    0.0f), DirectX::XMFLOAT2(rtex, ttex)};
			*pVertices++ = {DirectX::XMFLOAT3(right, bottom, 0.0f), DirectX::XMFLOAT2(rtex, btex)};
			nVertices += 6;

			if (nVertices > (MAX_NUM_VERTICES - 6)) {
				break;
			}
		}

		drawX += Width;
	}
	m_pDeviceContext->Unmap(m_pVertexBuffer, 0);

	UINT Stride = sizeof(VertexFont);
	UINT Offset = 0;
	ID3D11ShaderResourceView* views[1] = {};

	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

	D3D11_VIEWPORT VP;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	VP.Width    = rtSize.cx;
	VP.Height   = rtSize.cy;
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	m_pDeviceContext->RSSetViewports(1, &VP);

	m_pDeviceContext->PSSetShaderResources(0, 1, &m_pShaderResource);
	m_pDeviceContext->IASetInputLayout(m_pInputLayout);

	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &Stride, &Offset);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_pDeviceContext->VSSetShader(m_pVertexShader, nullptr, 0);
	m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);

	//m_pDeviceContext->OMSetBlendState(m_pBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	m_pDeviceContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);

	m_pDeviceContext->Draw(nVertices, 0);

	return S_OK;
}
