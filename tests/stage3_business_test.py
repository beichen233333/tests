import ctypes, json, pathlib, shutil, sys, tempfile
lib_path, source_assets = sys.argv[1], pathlib.Path(sys.argv[2])
lib = ctypes.CDLL(lib_path)
lib.AstraeOffline_Init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.AstraeOffline_Init.restype = ctypes.c_int
lib.AstraeOffline_Handle.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.AstraeOffline_Handle.restype = ctypes.c_void_p
lib.AstraeOffline_GetState.restype = ctypes.c_void_p
lib.AstraeOffline_ReplaceState.argtypes = [ctypes.c_char_p]
lib.AstraeOffline_ReplaceState.restype = ctypes.c_int
lib.AstraeOffline_Free.argtypes = [ctypes.c_void_p]
lib.AstraeOffline_Shutdown.argtypes = []
def take(ptr):
    if not ptr: return '{}'
    try: return ctypes.string_at(ptr).decode()
    finally: lib.AstraeOffline_Free(ptr)
def call(protocol, body):
    return json.loads(take(lib.AstraeOffline_Handle(protocol.encode(), json.dumps(body,separators=(',',':')).encode())))
def state(): return json.loads(take(lib.AstraeOffline_GetState()))
with tempfile.TemporaryDirectory(prefix='astrae-stage3-') as tmp:
    root=pathlib.Path(tmp); assets=root/'assets'; shutil.copytree(source_assets, assets)
    rules=json.loads((assets/'rules.json').read_text())
    rules['favorRewards']={'1100011':[{'Type':2,'TableId':4,'Count':123}]}
    rules['characterGrowthCosts']={
        'level':{'10001':{'2':[{'Type':5,'TableId':301,'Count':2}]}},
        'star':{'10001':{'2':[{'Type':5,'TableId':301,'Count':1}]}},
        'potential':{'10001':{'0':[{'Type':5,'TableId':301,'Count':1}]}},
        'resetPotential':{},
        'skill':{'10001':{'1':{'1':[{'Type':5,'TableId':301,'Count':1}]}}},
        'vignette':{}
    }
    rules['artifactGrowthCosts']={
        'level':{'7001':{'2':[{'Type':5,'TableId':301,'Count':3}]}},
        'star':{'7001':{'1':[{'Type':5,'TableId':301,'Count':2}]}}
    }
    rules['artifactDecomposeRewards']={'7001':[{'Type':2,'TableId':4,'Count':10}]}
    (assets/'rules.json').write_text(json.dumps(rules,separators=(',',':')))
    save=root/'save'
    assert lib.AstraeOffline_Init(str(save).encode(),str(assets).encode())==1
    seeded={'Account':{'ServerId':1,'UserName':'Stage3','NickName':'Stage3','Level':1,'Exp':0,'GameAccountId':'stage3','GameLocale':0},
            'inventory':[{'Type':5,'TableId':301,'Count':100},{'Type':5,'TableId':920001,'Count':3}],
            'characters':[{'TableId':10001,'ServerId':100001,'Level':1,'Star':1,'Potential':0,'Favorite':False,'Tags':[1,3]}],
            'GuideState':{'OfferingLevel':0},'DateFieldState':{'Favor':{'10001':{'FavorExp':0,'DateExp':0,'RewardFlag':0}}}}
    assert lib.AstraeOffline_ReplaceState(json.dumps(seeded,separators=(',',':')).encode())==1
    # GuideBook offering: table-defined cost 50 x item 301, plus table-defined rewards.
    offering=call('GuideBookOfferingLevelUp.Request',{'OfferingId':1})
    st=state(); assert st['GuideState']['OfferingLevel']==1
    inv={(x['Type'],x['TableId']):x['Count'] for x in st['inventory']}
    assert inv[(5,301)]==50 and inv[(5,3)]==1 and inv[(2,3)]==25
    # GuideBook task reward is read from Mission_Task and only claimable once.
    info=call('GuideBookMissionInfo.Request',{'MissionSystemType':8})
    assert len(info['GuideBookMissions'])==7
    clear_req={'MissionSystemType':8,'Chapter':1,'TaskId':70000010001}
    first=call('GuideBookMissionClear.Request',clear_req)
    st=state(); inv={(x['Type'],x['TableId']):x['Count'] for x in st['inventory']}
    assert inv[(5,301)]==75 and inv[(5,3)]==11
    call('GuideBookMissionClear.Request',clear_req)
    st=state(); inv={(x['Type'],x['TableId']):x['Count'] for x in st['inventory']}
    assert inv[(5,301)]==75 and inv[(5,3)]==11
    # DateField gift request uses NodeEventId + KeyValuePair<ulong,int>, consumes gifts and adds table favor exp.
    present=call('DateFieldEventPresent.Request',{'NodeEventId':10001005,'PresentItems':[{'Key':920001,'Value':2}]})
    st=state(); fav=st['DateFieldState']['Favor']['10001']
    inv={(x['Type'],x['TableId']):x['Count'] for x in st['inventory']}
    assert fav['FavorExp']==50 and fav['Level']==2 and inv[(5,920001)]==1
    assert present['ResultInfo']['FavorExpInfo']['AddedExp']==50
    # Favor reward is config-resolved and the source reward row is marked only after grant.
    claim=call('DateFieldClaimReward.Request',{'CharacterId':10001})
    st=state(); inv={(x['Type'],x['TableId']):x['Count'] for x in st['inventory']}
    assert inv[(2,4)]==123 and len(st['ClaimedFavorRewardIds'])==1
    call('DateFieldClaimReward.Request',{'CharacterId':10001})
    st=state(); inv={(x['Type'],x['TableId']):x['Count'] for x in st['inventory']}
    assert inv[(2,4)]==123 and len(st['ClaimedFavorRewardIds'])==1
    # Invalid/too many gifts must not mutate inventory or favor.
    before=state()
    call('DateFieldEventPresent.Request',{'NodeEventId':10001005,'PresentItems':[{'Key':920001,'Value':11}]})
    after=state(); assert before['DateFieldState']==after['DateFieldState'] and before['inventory']==after['inventory']
    # Character progression requires an owned character and an explicit configured recipe.
    growth_state={'Account':seeded['Account'],'inventory':[{'Type':5,'TableId':301,'Count':50},{'Type':5,'TableId':302,'Count':1}],
                  'characters':[{'TableId':10001,'ServerId':100001,'Level':1,'Star':1,'Potential':0,'Skills':[]}],
                  'artifacts':[{'TableId':7001,'ServerId':200001,'Level':1,'Star':0,'Locked':False},
                               {'TableId':7002,'ServerId':200002,'Level':1,'Star':0,'Locked':False},
                               {'TableId':7001,'ServerId':200003,'Level':1,'Star':0,'Locked':False},
                               {'TableId':7001,'ServerId':200004,'Level':1,'Star':0,'Locked':True}]}
    assert lib.AstraeOffline_ReplaceState(json.dumps(growth_state,separators=(',',':')).encode())==1
    char_key={'TableId':10001,'ServerId':100001}
    call('CharacterLevelUp.Request',{'CharacterKey':char_key,'TargetLevel':2})
    assert next(x for x in state()['characters'] if x['TableId']==10001)['Level']==2
    before=state(); call('CharacterLevelUp.Request',{'CharacterKey':char_key,'TargetLevel':3})
    assert state()['characters']==before['characters'] and state()['inventory']==before['inventory']
    call('CharacterLevelUp.Request',{'CharacterKey':{'TableId':77777,'ServerId':777777},'TargetLevel':2})
    assert state()['characters']==before['characters'] and state()['inventory']==before['inventory']
    call('CharacterStarRankUp.Request',{'CharacterKey':char_key,'TargetRank':2,'MaterialItemId':302})
    call('CharacterAddPotential.Request',{'CharacterKey':char_key,'TargetPotentialNode':0})
    call('CharacterSkillLevelUp.Request',{'CharacterKey':char_key,'TargetGroup':1,'TargetLevel':1})
    st=state(); ch=st['characters'][0]
    assert ch['Star']==2 and ch['Potential']==1 and ch['Skills'][0]['Level']==1
    inv={(x['Type'],x['TableId']):x['Count'] for x in st['inventory']}
    assert inv[(5,301)]==45 and inv[(5,302)]==0
    # Artifact growth requires explicit costs; star-up consumes unique owned unlocked material artifacts.
    call('ArtifactLevelUp.Request',{'ArtifactServerId':200001,'TargetLevel':2})
    before=state(); call('ArtifactLevelUp.Request',{'ArtifactServerId':200001,'TargetLevel':3})
    assert state()['artifacts']==before['artifacts'] and state()['inventory']==before['inventory']
    call('ArtifactStarRankUp.Request',{'ArtifactServerId':200001,'MaterialArtifactServerIds':[200002]})
    st=state(); main=next(x for x in st['artifacts'] if x['ServerId']==200001)
    assert main['Level']==2 and main['Star']==1 and not any(x['ServerId']==200002 for x in st['artifacts'])
    before=state(); call('ArtifactDecompose.Request',{'ArtifactServerIds':[200004]})
    assert state()['artifacts']==before['artifacts'] and state()['inventory']==before['inventory']
    call('ArtifactDecompose.Request',{'ArtifactServerIds':[200003]})
    st=state(); assert not any(x['ServerId']==200003 for x in st['artifacts'])
    inv={(x['Type'],x['TableId']):x['Count'] for x in st['inventory']}
    assert inv[(2,4)]==10
    lib.AstraeOffline_Shutdown()
print('stage3 table-driven progression tests passed')
