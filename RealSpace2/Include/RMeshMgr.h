#pragma once

#include "RMesh.h"
#include <vector>

_NAMESPACE_REALSPACE2_BEGIN

typedef list<RMesh*>			r_mesh_list;
typedef r_mesh_list::iterator	r_mesh_node;

#define MAX_NODE_TABLE 1000

class RMeshMgr
{
public:
	RMeshMgr();
	~RMeshMgr();

	int		Add(char* name,char* modelname=NULL,bool namesort=false);
	int		AddXml(char* name,char* modelname=NULL,bool autoload=true,bool namesort=false);
	int     AddXml(MXmlElement* pNode,char* Path,char* modelname=NULL,bool namesort=false);

	int		LoadXmlList(char* name,RFPROGRESSCALLBACK pfnProgressCallback=NULL, void *CallbackParam=NULL);

	void	Del(int id);
	void	Del(RMesh* pMesh);

	int		LoadList(char* name);
	int		SaveList(char* name);

	void	DelAll();

	void	ConnectMtrl();
	void	Render();
	void	Render(int id);

	void	RenderFast(int id,rmatrix* unit_mat);

	RMesh*	GetFast(int id);
	RMesh*	Get(const char* name);

	RMesh*	Load(const char* name);
	void	UnLoad(const char* name);

	void	LoadAll();
	void	UnLoadAll();

	void	CheckUnUsed();
	void	UnLoadChecked();

	void ReloadAllAnimation();

	void GetPartsNode(RMeshPartsType parts,vector<RMeshNode*>& nodetable);
	RMeshNode* GetPartsNode(const char* name);

	void SetMtrlAutoLoad(bool b) {
		m_mtrl_auto_load = b;
	}

	bool GetMtrlAutoLoad() {
		return m_mtrl_auto_load;
	}

	void SetMapObject(bool b) {
		m_is_map_object = b;
	}

	bool GetMapObject() {
		return m_is_map_object;
	}

	r_mesh_list m_list;
	int			m_id_last;

	bool		m_mtrl_auto_load;
	bool		m_is_map_object;

	vector<RMesh*> m_node_table;
	DWORD	m_cur;
};

_NAMESPACE_REALSPACE2_END